#include <ServerEngine/C/ServerEngine.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace asio = boost::asio;
using Tcp = asio::ip::tcp;
using Udp = asio::ip::udp;
using Bytes = std::vector<std::uint8_t>;

void check(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}

void success(se_status status, const se_error& error, const char* operation)
{
    if (status != SE_OK) throw std::runtime_error(std::string(operation) + ": " + error.message);
}

struct Host {
    se_server_handle handle{};
    se_error error{};

    explicit Host(std::uint32_t maximum_message_bytes = 128)
    {
        se_server_options options;
        se_server_options_init(&options);
        options.max_connections = 16;
        options.max_message_bytes = maximum_message_bytes;
        options.max_send_queue_bytes = maximum_message_bytes + 4096;
        options.max_event_queue_count = 256;
        options.max_event_queue_bytes = 64 * 1024;
        options.idle_timeout_ms = 10000;
        success(se_server_create(&options, &handle, &error), error, "create server");
    }

    ~Host() { if (handle) (void)se_server_destroy(handle, nullptr); }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    se_status add(std::uint32_t protocol, std::uint16_t port,
        std::uint32_t security = SE_SECURITY_NONE)
    {
        se_listener_options listener;
        se_listener_options_init(&listener);
        listener.protocol = protocol;
        listener.security = security;
        listener.port = port;
        listener.bind_address = "127.0.0.1";
        listener.websocket_path = "/game";
        listener.handshake_timeout_ms = 500;
        std::uint64_t id{};
        return se_server_add_listener(handle, &listener, &id, &error);
    }

    void start() { success(se_server_start(handle, &error), error, "start server"); }
    void send(std::uint64_t session, const Bytes& message)
    {
        success(se_server_send(handle, session, message.data(),
            static_cast<std::uint32_t>(message.size()), &error), error, "send message");
    }
};

struct Event {
    se_event metadata{};
    Bytes payload;
};

Event await_event(Host& host, std::uint32_t kind, std::uint64_t session = 0)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        Event event;
        se_event_init(&event.metadata);
        std::array<std::uint8_t, 4096> bytes{};
        const auto status = se_server_poll_event(host.handle, &event.metadata, bytes.data(),
            static_cast<std::uint32_t>(bytes.size()), 100, &host.error);
        if (status == SE_TIMEOUT) continue;
        success(status, host.error, "poll event");
        event.payload.assign(bytes.begin(), bytes.begin() + event.metadata.payload_size);
        if (event.metadata.kind == kind && (session == 0 || event.metadata.session_id == session))
            return event;
        if (event.metadata.kind == SE_EVENT_MESSAGE)
            throw std::runtime_error("Unexpected application message while waiting for an event");
        check(event.metadata.kind != SE_EVENT_OVERFLOW, "Event queue must not overflow in loopback tests");
    }
    throw std::runtime_error("Timed out waiting for transport event");
}

void expect_quiet(Host& host)
{
    se_event event;
    se_event_init(&event);
    std::array<std::uint8_t, 1024> bytes{};
    check(se_server_poll_event(host.handle, &event, bytes.data(),
        static_cast<std::uint32_t>(bytes.size()), 50, &host.error) == SE_TIMEOUT,
        "Incomplete TCP frames must not emit messages");
}

Bytes frame(const Bytes& payload)
{
    const auto size = static_cast<std::uint32_t>(payload.size());
    Bytes result{static_cast<std::uint8_t>(size >> 24), static_cast<std::uint8_t>(size >> 16),
        static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size)};
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

Bytes read_tcp_frame(Tcp::socket& socket)
{
    std::array<std::uint8_t, 4> header{};
    asio::read(socket, asio::buffer(header));
    const std::uint32_t size = (std::uint32_t(header[0]) << 24) |
        (std::uint32_t(header[1]) << 16) | (std::uint32_t(header[2]) << 8) | header[3];
    check(size <= 128, "Server TCP frame has an invalid length");
    Bytes payload(size);
    asio::read(socket, asio::buffer(payload));
    return payload;
}

std::uint64_t test_tcp(Host& host, Tcp::socket& socket, std::uint16_t port)
{
    socket.connect({asio::ip::address_v4::loopback(), port});
    const auto opened = await_event(host, SE_EVENT_OPEN);
    check(opened.metadata.protocol == SE_PROTOCOL_TCP, "Expected a TCP session");
    const auto session = opened.metadata.session_id;
    const Bytes payload{'A', 0, 0xff, 'B'};
    const auto wire = frame(payload);

    asio::write(socket, asio::buffer(wire.data(), 1));
    expect_quiet(host);
    asio::write(socket, asio::buffer(wire.data() + 1, 3));
    expect_quiet(host);
    asio::write(socket, asio::buffer(wire.data() + 4, 2));
    expect_quiet(host);
    asio::write(socket, asio::buffer(wire.data() + 6, 2));
    check(await_event(host, SE_EVENT_MESSAGE, session).payload == payload,
        "Split TCP header/body must preserve binary bytes");
    host.send(session, payload);
    check(read_tcp_frame(socket) == payload, "C ABI send must add the TCP length prefix");

    const Bytes first{'o', 'n', 'e'};
    const Bytes second{'t', 'w', 'o'};
    auto combined = frame(first);
    const auto next = frame(second);
    combined.insert(combined.end(), next.begin(), next.end());
    asio::write(socket, asio::buffer(combined));
    check(await_event(host, SE_EVENT_MESSAGE, session).payload == first,
        "First coalesced TCP frame must be separate");
    check(await_event(host, SE_EVENT_MESSAGE, session).payload == second,
        "Second coalesced TCP frame must be separate");
    check(se_server_send(host.handle, UINT64_MAX, payload.data(),
        static_cast<std::uint32_t>(payload.size()), &host.error) < 0,
        "Sending to an unknown session must fail");
    return session;
}

void test_udp(Host& host, asio::io_context& io, std::uint16_t port)
{
    Udp::socket socket(io, Udp::endpoint(asio::ip::address_v4::loopback(), 0));
    const Udp::endpoint endpoint(asio::ip::address_v4::loopback(), port);
    const Bytes empty;
    socket.send_to(asio::buffer(empty), endpoint);
    const auto opened = await_event(host, SE_EVENT_OPEN);
    check(opened.metadata.protocol == SE_PROTOCOL_UDP, "Expected a UDP endpoint session");
    const auto session = opened.metadata.session_id;
    check(await_event(host, SE_EVENT_MESSAGE, session).payload.empty(),
        "Zero-length UDP datagram must be delivered");
    host.send(session, empty);
    std::array<std::uint8_t, 256> received{};
    Udp::endpoint sender;
    check(socket.receive_from(asio::buffer(received), sender) == 0,
        "C ABI must send a zero-length UDP datagram");

    Bytes exact(128);
    for (std::size_t index = 0; index < exact.size(); ++index)
        exact[index] = static_cast<std::uint8_t>(index);
    socket.send_to(asio::buffer(exact), endpoint);
    check(await_event(host, SE_EVENT_MESSAGE, session).payload == exact,
        "UDP exact-limit datagram must remain one binary message");
    host.send(session, exact);
    const auto count = socket.receive_from(asio::buffer(received), sender);
    check(Bytes(received.begin(), received.begin() + count) == exact,
        "UDP reply must preserve exact datagram boundaries");
}

void test_websocket(Host& host, asio::io_context& io, std::uint16_t port)
{
    boost::beast::websocket::stream<Tcp::socket> socket(io);
    socket.next_layer().connect({asio::ip::address_v4::loopback(), port});
    socket.handshake("127.0.0.1", "/game");
    const auto opened = await_event(host, SE_EVENT_OPEN);
    check(opened.metadata.protocol == SE_PROTOCOL_WEBSOCKET, "Expected a WebSocket session");
    const auto session = opened.metadata.session_id;
    const Bytes payload{'W', 0, 0xfe, 'S'};
    socket.binary(true);
    socket.write(asio::buffer(payload));
    check(await_event(host, SE_EVENT_MESSAGE, session).payload == payload,
        "WebSocket binary payload must preserve NUL bytes");
    host.send(session, payload);
    boost::beast::flat_buffer incoming;
    socket.read(incoming);
    Bytes reply(incoming.size());
    asio::buffer_copy(asio::buffer(reply), incoming.data());
    check(!socket.got_text() && reply == payload, "C ABI WebSocket send must produce binary frames");

    socket.text(true);
    socket.write(asio::buffer("text", 4));
    (void)await_event(host, SE_EVENT_CLOSE, session);
}

void test_websocket_upgrade_buffer(asio::io_context& io)
{
    Tcp::acceptor reservation(io, {asio::ip::address_v4::loopback(), 0});
    const auto port = reservation.local_endpoint().port();
    Host host(4096);
    success(host.add(SE_PROTOCOL_WEBSOCKET, port), host.error, "add buffered WebSocket");
    reservation.close();
    host.start();
    Tcp::socket socket(io);
    socket.connect({asio::ip::address_v4::loopback(), port});
    const std::string request = "GET /game HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\nX-Test-Padding: " + std::string(2048, 'a') + "\r\n\r\n";
    Bytes wire(request.begin(), request.end());
    Bytes payload(2048);
    for (std::size_t index = 0; index < payload.size(); ++index)
        payload[index] = static_cast<std::uint8_t>(index);
    const std::array<std::uint8_t, 4> mask{0x12, 0x34, 0x56, 0x78};
    wire.push_back(0x82); // FIN + binary opcode.
    wire.push_back(0xfe); // MASK + 16-bit length marker.
    wire.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
    wire.push_back(static_cast<std::uint8_t>(payload.size()));
    wire.insert(wire.end(), mask.begin(), mask.end());
    for (std::size_t index = 0; index < payload.size(); ++index)
        wire.push_back(static_cast<std::uint8_t>(payload[index] ^ mask[index % mask.size()]));
    asio::write(socket, asio::buffer(wire));

    asio::streambuf response;
    asio::read_until(socket, response, "\r\n\r\n");
    std::istream response_text(&response);
    std::string status_line;
    std::getline(response_text, status_line);
    check(status_line.find("HTTP/1.1 101 ") == 0,
        "WebSocket upgrade headers larger than 1536 bytes must be accepted");
    const auto opened = await_event(host, SE_EVENT_OPEN);
    check(opened.metadata.protocol == SE_PROTOCOL_WEBSOCKET, "Expected buffered WebSocket session");
    check(await_event(host, SE_EVENT_MESSAGE, opened.metadata.session_id).payload == payload,
        "First frame pipelined after HTTP headers must not be lost");
}

void test_startup_rollback(asio::io_context& io)
{
    Tcp::acceptor first(io, {asio::ip::address_v4::loopback(), 0});
    Tcp::acceptor occupied(io, {asio::ip::address_v4::loopback(), 0});
    const auto first_port = first.local_endpoint().port();
    const auto occupied_port = occupied.local_endpoint().port();
    Host host;
    success(host.add(SE_PROTOCOL_TCP, first_port), host.error, "add first rollback listener");
    success(host.add(SE_PROTOCOL_TCP, occupied_port), host.error, "add occupied rollback listener");
    first.close();
    check(se_server_start(host.handle, &host.error) < 0, "Occupied second listener must fail startup");
    // A failed second bind must release the first listener's successful bind.
    Tcp::acceptor probe(io, {asio::ip::address_v4::loopback(), first_port});
    probe.close();
    occupied.close();
    host.start();
    success(se_server_stop(host.handle, &host.error), host.error, "stop retry after rollback");
}

void test_encrypted_udp_is_rejected(asio::io_context& io)
{
    Udp::socket reservation(io, Udp::endpoint(asio::ip::address_v4::loopback(), 0));
    const auto port = reservation.local_endpoint().port();
    Host host;
    const auto added = host.add(SE_PROTOCOL_UDP, port, SE_SECURITY_TLS);
    reservation.close();
    check(added < 0 || se_server_start(host.handle, &host.error) < 0,
        "TLS requested on UDP must fail closed, never become plaintext");
    Udp::socket probe(io, Udp::endpoint(asio::ip::address_v4::loopback(), port));
}

} // namespace

int main()
{
    try {
        asio::io_context io;
        test_startup_rollback(io);
        test_encrypted_udp_is_rejected(io);
        test_websocket_upgrade_buffer(io);

        // OS-selected ports are held until immediately before start. CTest's
        // 30-second process timeout also bounds blocking client I/O on failures.
        Tcp::acceptor tcp_port(io, {asio::ip::address_v4::loopback(), 0});
        Tcp::acceptor ws_port(io, {asio::ip::address_v4::loopback(), 0});
        Udp::socket udp_port(io, Udp::endpoint(asio::ip::address_v4::loopback(), 0));
        const auto tcp_number = tcp_port.local_endpoint().port();
        const auto ws_number = ws_port.local_endpoint().port();
        const auto udp_number = udp_port.local_endpoint().port();
        Host host;
        success(host.add(SE_PROTOCOL_TCP, tcp_number), host.error, "add TCP");
        success(host.add(SE_PROTOCOL_WEBSOCKET, ws_number), host.error, "add WS");
        success(host.add(SE_PROTOCOL_UDP, udp_number), host.error, "add UDP");
        tcp_port.close();
        ws_port.close();
        udp_port.close();
        host.start();

        Tcp::socket tcp(io);
        const auto tcp_session = test_tcp(host, tcp, tcp_number);
        test_udp(host, io, udp_number);
        test_websocket(host, io, ws_number);
        // Leave a partially received header outstanding while shutdown joins.
        const std::array<std::uint8_t, 2> partial_header{};
        asio::write(tcp, asio::buffer(partial_header));
        success(se_server_stop(host.handle, &host.error), host.error, "stop with pending reads");
        check(se_server_send(host.handle, tcp_session, nullptr, 0, &host.error) < 0,
            "Stopped server must reject sends");
        success(se_server_stop(host.handle, &host.error), host.error, "idempotent stop");
        std::cout << "PASS TransportLoopback\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "FAIL TransportLoopback: " << failure.what() << '\n';
        return 1;
    }
}
