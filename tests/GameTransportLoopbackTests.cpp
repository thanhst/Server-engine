#include <ServerEngine/C/GameTransport.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto WaitLimit = std::chrono::seconds(8);

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void success(se_status status, const char* operation)
{
    if (status != SE_OK) throw std::runtime_error(std::string(operation) + ": status=" + std::to_string(status));
}
struct Endpoint {
    se_game_handle handle{};
    ~Endpoint() { if (handle) se_game_destroy(handle, nullptr); }
    Endpoint() = default;
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;
};
struct Event {
    se_game_event metadata{};
    std::array<char, 8192> payload{};
    std::string_view bytes() const { return {payload.data(), metadata.payload_size}; }
};

bool poll(Endpoint& endpoint, Event& event, std::uint32_t wait = 0)
{
    se_game_event_init(&event.metadata);
    const auto status = se_game_poll(endpoint.handle, &event.metadata, event.payload.data(),
        static_cast<std::uint32_t>(event.payload.size()), wait, nullptr);
    if (status == SE_TIMEOUT) return false;
    success(status, "poll loopback");
    check(event.metadata.kind != SE_GAME_OVERFLOW, "loopback event queue overflowed");
    return true;
}

std::uint32_t listen(Endpoint& server)
{
    const auto seed = static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
    // Bind through our own ABI. No OS/vendor socket headers and no release/bind
    // reservation race. Concurrent test runs select different candidate ranges.
    for (std::uint32_t attempt = 0; attempt < 64; ++attempt) {
        const auto port = static_cast<std::uint32_t>(30000 + (seed + attempt) % 20000);
        if (se_game_listen(server.handle, "127.0.0.1", port, nullptr) == SE_OK) {
            check(se_game_listen(server.handle, "127.0.0.1", port, nullptr) == SE_INVALID_STATE,
                "second listener on endpoint rejected");
            return port;
        }
    }
    throw std::runtime_error("Could not bind any loopback test port");
}

std::uint64_t await_connection(Endpoint& server, Endpoint& client, std::uint64_t client_peer)
{
    std::uint64_t server_peer{};
    bool client_connected = false;
    const auto deadline = Clock::now() + WaitLimit;
    while (Clock::now() < deadline && (!server_peer || !client_connected)) {
        Event event;
        if (poll(server, event)) {
            check(event.metadata.kind == SE_GAME_CONNECTED, "server expects connected event");
            server_peer = event.metadata.peer_id;
        }
        if (poll(client, event, 5)) {
            check(event.metadata.kind == SE_GAME_CONNECTED && event.metadata.peer_id == client_peer,
                "client connected event matches returned peer ID");
            client_connected = true;
        }
    }
    check(server_peer != 0 && client_connected, "connect completed at both endpoints");
    return server_peer;
}

void buffer_retry(Endpoint& server, Endpoint& client, std::uint64_t server_peer, std::uint64_t client_peer)
{
    std::string payload(513, 'x');
    payload[0] = '\0';
    payload[1] = static_cast<char>(0xff);
    const auto original = payload;
    success(se_game_send(client.handle, client_peer, SE_GAME_RELIABLE_ORDERED,
        payload.data(), static_cast<std::uint32_t>(payload.size()), nullptr), "send binary message");
    payload.assign(payload.size(), 'z'); // DLL must have copied it before returning.
    const auto deadline = Clock::now() + WaitLimit;
    se_game_event probe;
    bool found = false;
    while (Clock::now() < deadline) {
        Event ignored;
        check(!poll(client, ignored, 5), "unexpected client event during binary transfer");
        se_game_event_init(&probe);
        const auto status = se_game_poll(server.handle, &probe, nullptr, 0, 0, nullptr);
        if (status == SE_TIMEOUT) continue;
        check(status == SE_BUFFER_TOO_SMALL && probe.kind == SE_GAME_MESSAGE
            && probe.peer_id == server_peer && probe.delivery == SE_GAME_RELIABLE_ORDERED
            && probe.payload_size == original.size(), "size probe reports retained binary message");
        found = true;
        break;
    }
    check(found, "binary message arrived before deadline");
    Event event;
    check(poll(server, event) && event.metadata.peer_id == probe.peer_id && event.bytes() == original,
        "buffer retry returns same event and original copied bytes");
    success(se_game_send(server.handle, server_peer, SE_GAME_RELIABLE_ORDERED, "PONG", 4, nullptr), "send reply");
    const auto reply_deadline = Clock::now() + WaitLimit;
    bool reply = false;
    while (Clock::now() < reply_deadline && !reply) {
        check(!poll(server, event), "unexpected server event while waiting for reply");
        if (poll(client, event, 5)) {
            check(event.metadata.kind == SE_GAME_MESSAGE && event.metadata.delivery == SE_GAME_RELIABLE_ORDERED
                && event.bytes() == "PONG", "client received reliable reply");
            reply = true;
        }
    }
    check(reply, "reliable reply arrived");
}

void reliable_order(Endpoint& server, Endpoint& client, std::uint64_t client_peer)
{
    constexpr unsigned Count = 16;
    for (unsigned i = 0; i < Count; ++i) {
        const auto message = "ordered-" + std::to_string(i);
        success(se_game_send(client.handle, client_peer, SE_GAME_RELIABLE_ORDERED,
            message.data(), static_cast<std::uint32_t>(message.size()), nullptr), "queue ordered message");
    }
    unsigned received = 0;
    const auto deadline = Clock::now() + WaitLimit;
    while (Clock::now() < deadline && received < Count) {
        Event event;
        check(!poll(client, event, 5), "unexpected client event during ordered transfer");
        if (poll(server, event)) {
            check(event.metadata.kind == SE_GAME_MESSAGE && event.metadata.delivery == SE_GAME_RELIABLE_ORDERED
                && event.bytes() == "ordered-" + std::to_string(received), "reliable messages delivered in order without duplicates");
            ++received;
        }
    }
    check(received == Count, "all reliable messages arrived");
}

void unreliable_reception(Endpoint& server, Endpoint& client, std::uint64_t client_peer)
{
    const auto deadline = Clock::now() + WaitLimit;
    auto next_send = Clock::now();
    unsigned sent = 0;
    bool received = false;
    while (Clock::now() < deadline && !received) {
        if (Clock::now() >= next_send) {
            const auto snapshot = "snapshot-" + std::to_string(sent++);
            success(se_game_send(client.handle, client_peer, SE_GAME_UNRELIABLE,
                snapshot.data(), static_cast<std::uint32_t>(snapshot.size()), nullptr), "queue unreliable snapshot");
            next_send = Clock::now() + std::chrono::milliseconds(20);
        }
        Event event;
        check(!poll(client, event, 5), "unexpected client event during unreliable transfer");
        if (poll(server, event)) {
            check(event.metadata.kind == SE_GAME_MESSAGE && event.metadata.delivery == SE_GAME_UNRELIABLE
                && event.bytes().substr(0, 9) == "snapshot-", "unreliable message type and payload");
            received = true;
        }
    }
    // Loopback sanity only: deliberately no exact count or ordering assertion.
    check(received, "at least one of repeated local unreliable snapshots arrived");
}

void disconnect_and_wake(Endpoint& server, Endpoint& client, std::uint64_t server_peer, std::uint64_t client_peer)
{
    success(se_game_disconnect(client.handle, client_peer, nullptr), "disconnect client peer");
    const auto stale = se_game_send(client.handle, client_peer, SE_GAME_RELIABLE_ORDERED, "x", 1, nullptr);
    check(stale == SE_INVALID_HANDLE || stale == SE_INVALID_STATE, "disconnected peer rejects new send");
    bool server_closed = false;
    const auto deadline = Clock::now() + WaitLimit;
    while (Clock::now() < deadline && !server_closed) {
        Event event;
        (void)poll(client, event, 5);
        if (poll(server, event) && event.metadata.kind == SE_GAME_DISCONNECTED) {
            check(event.metadata.peer_id == server_peer, "disconnect maps to server peer");
            server_closed = true;
        }
    }
    check(server_closed, "remote disconnect event arrived");
    // Empty endpoint avoids a racing disconnect event satisfying the blocking poll.
    Endpoint waiting;
    se_game_options options;
    se_game_options_init(&options);
    success(se_game_create(&options, &waiting.handle, nullptr), "create waiting endpoint");
    const auto waiting_handle = waiting.handle;
    std::promise<void> entering;
    auto entered = entering.get_future();
    auto waiter = std::async(std::launch::async, [&] {
        se_game_event event;
        se_game_event_init(&event);
        entering.set_value();
        return se_game_poll(waiting_handle, &event, nullptr, 0, 30000, nullptr);
    });
    entered.wait();
    success(se_game_destroy(waiting.handle, nullptr), "destroy wakes waiter");
    waiting.handle = 0;
    check(waiter.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "poll waiter woke promptly");
    const auto status = waiter.get();
    check(status == SE_STOPPED || status == SE_INVALID_HANDLE, "poll sees closed or already-removed endpoint");
}
} // namespace

int main()
{
    try {
        check(se_game_get_abi_version() == SE_GAME_ABI_VERSION, "game ABI version");
        Endpoint server, client;
        se_game_options options;
        se_game_options_init(&options);
        options.max_message_bytes = 4096;
        const auto capability = se_game_create(&options, &server.handle, nullptr);
        if (capability == SE_NOT_SUPPORTED) {
            check(server.handle == 0, "disabled create clears handle");
            std::cout << "SKIP game transport loopback: feature disabled\n";
            return 77;
        }
        success(capability, "create server endpoint");
        success(se_game_create(&options, &client.handle, nullptr), "create client endpoint");
        const auto port = listen(server);
        std::uint64_t client_peer{};
        success(se_game_connect(client.handle, "127.0.0.1", port, &client_peer, nullptr), "connect client");
        const auto server_peer = await_connection(server, client, client_peer);
        buffer_retry(server, client, server_peer, client_peer);
        reliable_order(server, client, client_peer);
        unreliable_reception(server, client, client_peer);
        disconnect_and_wake(server, client, server_peer, client_peer);
        std::cout << "PASS game transport loopback, ordered stream, caller buffers and lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL game transport loopback: " << error.what() << '\n';
        return 1;
    }
}
