#include "GameServer.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require_ok(se_status result, const se_error& error)
{
    if (result != SE_OK) throw std::runtime_error(error.message);
}
}

GameServer::GameServer(const char* certificate, const char* private_key, const char* database)
    : store_(database)
{
    if (se_get_abi_version() != SE_ABI_VERSION) throw std::runtime_error("ServerEngine DLL ABI mismatch");
    se_server_options options;
    se_server_options_init(&options);
    options.max_connections = 1024;
    options.max_message_bytes = 4096;
    options.max_send_queue_bytes = 65536;
    options.idle_timeout_ms = 60000;
    se_error error{};
    require_ok(se_server_create(&options, &engine_.value, &error), error);
    add_listener(SE_PROTOCOL_TCP, 9443, certificate, private_key);
    add_listener(SE_PROTOCOL_UDP, 9001, nullptr, nullptr);
    add_listener(SE_PROTOCOL_WEBSOCKET, 9444, certificate, private_key);
    require_ok(se_server_start(engine_.value, &error), error);
}

void GameServer::add_listener(std::uint32_t protocol, std::uint32_t port,
    const char* certificate, const char* private_key)
{
    se_listener_options listener;
    se_listener_options_init(&listener);
    listener.protocol = protocol;
    listener.port = port;
    listener.bind_address = "127.0.0.1";
    listener.security = protocol == SE_PROTOCOL_UDP ? SE_SECURITY_NONE : SE_SECURITY_TLS;
    listener.certificate_chain_file = certificate;
    listener.private_key_file = private_key;
    listener.websocket_path = "/game";
    std::uint64_t id{};
    se_error error{};
    require_ok(se_server_add_listener(engine_.value, &listener, &id, &error), error);
    std::cout << "Configured listener=" << id << " protocol=" << protocol << " 127.0.0.1:" << port << '\n';
}

GameServer::~GameServer()
{
    se_server_stop(engine_.value, nullptr);
    for (const auto& entry : players_) {
        if (!entry.second.logging_requested) continue;
        try { store_.close_connection(entry.first, entry.second.messages); }
        catch (const std::exception& error) { std::cerr << "DB shutdown: " << error.what() << '\n'; }
    }
    // Allow five seconds for graceful draining after networking stops. SQL stop
    // then joins/cancels cooperatively; a blocked disk call can extend shutdown.
    // Drain committed/cancelled outcomes before destroying the DLL handle.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    try {
        PlayerStore::Completion completion;
        while (store_.pending_count() && std::chrono::steady_clock::now() < deadline) {
            if (store_.poll(completion, 100) && completion.status != SE_OK)
                std::cerr << "DB shutdown request=" << completion.request_id << ": " << completion.message << '\n';
        }
        store_.stop();
        while (store_.pending_count() && store_.poll(completion)) {
            if (completion.status != SE_OK)
                std::cerr << "DB shutdown request=" << completion.request_id << " status=" << completion.status
                          << ": " << completion.message << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "DB shutdown drain: " << error.what() << '\n';
    }
}

void GameServer::run(const std::atomic_bool& stop_requested)
{
    std::vector<char> payload(1024);
    while (!stop_requested.load(std::memory_order_relaxed)) {
        poll_database(); // Nonblocking: database work never runs inside the game tick.
        se_event event;
        se_event_init(&event);
        se_error error{};
        const auto status = se_server_poll_event(engine_.value, &event, payload.data(),
            static_cast<std::uint32_t>(payload.size()), 10, &error);
        if (status == SE_TIMEOUT) continue;
        if (status == SE_STOPPED) break;
        if (status == SE_BUFFER_TOO_SMALL) {
            payload.resize(event.payload_size); // Event stays queued until next poll.
            continue;
        }
        require_ok(status, error);
        std::cout << "event=" << event.sequence << " listener=" << event.listener_id
                  << " session=" << event.session_id << " kind=" << event.kind << '\n';
        handle_event(event, std::string_view(payload.data(), event.payload_size));
    }
}

void GameServer::handle_event(const se_event& event, std::string_view payload)
{
    if (event.kind == SE_EVENT_OVERFLOW)
        throw std::runtime_error("Event overflow: state is incomplete; recreate server before accepting players");
    if (event.kind == SE_EVENT_ERROR) {
        std::cerr << "Transport listener=" << event.listener_id << ": " << payload << '\n';
    } else if (event.kind == SE_EVENT_OPEN) {
        PlayerSession player;
        if (event.protocol != SE_PROTOCOL_UDP) {
            try {
                store_.open_connection(event.session_id, event.protocol, event.peer_address);
                player.logging_requested = true;
            } catch (const std::exception& error) {
                std::cerr << "DB admission session=" << event.session_id << ": " << error.what() << '\n';
                se_server_disconnect(engine_.value, event.session_id, nullptr);
            }
        }
        players_.emplace(event.session_id, std::move(player));
    } else if (event.kind == SE_EVENT_MESSAGE) {
        handle_message(event, payload);
    } else if (event.kind == SE_EVENT_CLOSE) {
        const auto player = players_.find(event.session_id);
        if (player == players_.end()) return;
        if (player->second.logging_requested) {
            try { store_.close_connection(event.session_id, player->second.messages); }
            catch (const std::exception& error) {
                std::cerr << "DB close session=" << event.session_id << ": " << error.what() << '\n';
            }
        }
        players_.erase(player);
    }
}

void GameServer::handle_message(const se_event& event, std::string_view payload)
{
    if (event.protocol == SE_PROTOCOL_UDP) {
        // A source IP/port is spoofable. UDP exposes only non-sensitive echo.
        reply(event.session_id, payload == "PING" ? std::string_view("PONG") : payload);
        return;
    }
    auto& player = players_.at(event.session_id);
    if (!player.logging_requested) return;
    if (player.messages == (std::numeric_limits<std::int64_t>::max)())
        throw std::overflow_error("Message counter exhausted");
    try { store_.save_message_count(event.session_id, ++player.messages); }
    catch (const std::exception& error) {
        std::cerr << "DB busy session=" << event.session_id << ": " << error.what() << '\n';
        reply(event.session_id, "ERR database busy; request rejected");
        return;
    }
    if (payload == "PING") {
        reply(event.session_id, "PONG");
    } else if (payload == "STATS") {
        reply(event.session_id, "messages=" + std::to_string(player.messages));
    } else if (payload == "HISTORY") {
        try { store_.load_history(event.session_id); }
        catch (const std::exception& error) {
            std::cerr << "DB history session=" << event.session_id << ": " << error.what() << '\n';
            reply(event.session_id, "ERR database busy; history request rejected");
        }
    } else if (payload.substr(0, 5) == "NAME ") {
        const auto name = payload.substr(5);
        const bool valid = !name.empty() && name.size() <= 48 &&
            std::all_of(name.begin(), name.end(), [](unsigned char byte) { return byte >= 32 && byte < 127; });
        if (!valid) { reply(event.session_id, "ERR name must contain 1..48 printable ASCII bytes"); return; }
        player.display_name.assign(name.data(), name.size());
        reply(event.session_id, "OK display name=" + player.display_name);
    } else {
        reply(event.session_id, "ERR commands: PING | NAME <display-name> | STATS | HISTORY");
    }
}

void GameServer::poll_database()
{
    PlayerStore::Completion completion;
    for (int i = 0; i < 64 && store_.poll(completion); ++i) {
        std::cout << "sql request=" << completion.request_id << " session=" << completion.session
                  << " operation=" << static_cast<int>(completion.operation) << " status=" << completion.status << '\n';
        const bool connected = players_.find(completion.session) != players_.end();
        if (completion.status != SE_OK) {
            std::cerr << "SQL request=" << completion.request_id << ": " << completion.message << '\n';
            if (connected) {
                reply(completion.session, "ERR database operation failed");
                if (completion.operation != PlayerStore::Operation::History)
                    se_server_disconnect(engine_.value, completion.session, nullptr);
            }
        } else if (connected && completion.operation == PlayerStore::Operation::History) {
            reply(completion.session, "stored_messages=" + std::to_string(completion.stored_messages));
        }
    }
}

void GameServer::reply(std::uint64_t session, std::string_view text)
{
    se_error error{};
    const auto status = se_server_send(engine_.value, session, text.data(),
        static_cast<std::uint32_t>(text.size()), &error);
    if (status != SE_OK) {
        std::cerr << "Send session=" << session << " status=" << status << ": " << error.message << '\n';
        se_server_disconnect(engine_.value, session, nullptr); // Isolate a slow/closed peer.
    }
}
