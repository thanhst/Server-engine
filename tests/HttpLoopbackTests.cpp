#include <ServerEngine/C/Http.h>

#include "HttpTestCertificate.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace asio = boost::asio;
namespace http = boost::beast::http;
using Tcp = asio::ip::tcp;
using Bytes = std::vector<std::uint8_t>;

static_assert(sizeof(se_http_request) == 48, "HTTP v1 request metadata layout");
static_assert(sizeof(se_http_response) == (sizeof(void*) == 8 ? 80 : 72), "HTTP v1 response layout");

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void success(se_status status, const se_error& error)
{
    if (status != SE_OK) throw std::runtime_error(error.message);
}

struct Host {
    se_server_handle handle{};
    se_error error{};
    std::uint16_t port{};

    explicit Host(asio::io_context& io, const HttpTestCertificate* certificate = nullptr,
        std::uint32_t idle_timeout = 5000)
    {
        // Port reservation is released immediately before start. RUN_SERIAL
        // reduces other tests competing; an unrelated process can still race.
        Tcp::acceptor reserved(io, {asio::ip::address_v4::loopback(), 0});
        port = reserved.local_endpoint().port();
        se_server_options options;
        se_server_options_init(&options);
        options.max_message_bytes = 4096;
        options.max_send_queue_bytes = 32768;
        options.max_event_queue_count = 64;
        options.max_event_queue_bytes = 65536;
        options.idle_timeout_ms = idle_timeout;
        success(se_server_create(&options, &handle, &error), error);
        try {
            se_listener_options listener;
            se_listener_options_init(&listener);
            listener.protocol = SE_PROTOCOL_HTTP;
            listener.security = certificate ? SE_SECURITY_TLS : SE_SECURITY_NONE;
            listener.port = port;
            listener.handshake_timeout_ms = 2000;
            if (certificate) {
                listener.certificate_chain_file = certificate->certificate_file.c_str();
                listener.private_key_file = certificate->key_file.c_str();
            }
            std::uint64_t id{};
            success(se_server_add_listener(handle, &listener, &id, &error), error);
            reserved.close();
            success(se_server_start(handle, &error), error);
        } catch (...) {
            (void)se_server_destroy(handle, nullptr);
            handle = 0;
            throw;
        }
    }

    ~Host() { if (handle) (void)se_server_destroy(handle, nullptr); }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
};

struct Request {
    se_event event{};
    se_http_request view{};
    Bytes bytes;
    std::string text(std::uint32_t offset, std::uint32_t size) const
    {
        return {reinterpret_cast<const char*>(bytes.data() + offset), size};
    }
};

Request next_request(Host& host)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        Request request;
        se_event_init(&request.event);
        request.bytes.resize(4096);
        const auto status = se_server_poll_event(host.handle, &request.event, request.bytes.data(),
            static_cast<std::uint32_t>(request.bytes.size()), 100, &host.error);
        if (status == SE_TIMEOUT) continue;
        success(status, host.error);
        if (request.event.kind == SE_EVENT_OPEN) continue;
        check(request.event.kind == SE_EVENT_HTTP_REQUEST, "Expected HTTP request event");
        request.bytes.resize(request.event.payload_size);
        success(se_http_request_read(request.bytes.data(), request.event.payload_size,
            &request.view, &host.error), host.error);
        check(request.event.protocol == SE_PROTOCOL_HTTP, "HTTP event must identify HTTP protocol");
        return request;
    }
    throw std::runtime_error("HTTP request event timed out");
}

void respond(Host& host, const Request& request, const Bytes& body, unsigned status = 200)
{
    se_http_response response;
    se_http_response_init(&response);
    response.status_code = status;
    response.content_type = "application/octet-stream";
    response.body = body.data();
    response.body_size = static_cast<std::uint32_t>(body.size());
    success(se_http_respond(host.handle, request.event.session_id, request.view.request_id,
        &response, &host.error), host.error);
}

template<class Stream>
auto read_response(Stream& socket, bool head = false)
{
    boost::beast::flat_buffer buffer;
    http::response_parser<http::vector_body<std::uint8_t>> parser;
    parser.body_limit(4096);
    if (head) parser.skip(true);
    http::read(socket, buffer, parser);
    return parser.release();
}

void test_binary_and_keepalive(asio::io_context& io)
{
    Host host(io);
    Tcp::socket socket(io);
    socket.connect({asio::ip::address_v4::loopback(), host.port});
    const Bytes binary{'A', 0, 0xff, 'B'};
    http::request<http::vector_body<std::uint8_t>> message(http::verb::post, "/echo?raw=%00", 11);
    message.set(http::field::host, "localhost");
    message.set("X-Trace", "binary-test");
    message.body() = binary;
    message.prepare_payload();
    http::write(socket, message);
    const auto first = next_request(host);
    check(first.text(first.view.method_offset, first.view.method_size) == "POST" &&
        first.text(first.view.target_offset, first.view.target_size) == "/echo?raw=%00",
        "HTTP method and escaped target must survive event serialization");
    check(Bytes(first.bytes.begin() + first.view.body_offset, first.bytes.end()) == binary,
        "HTTP body must preserve NUL and non-UTF8 bytes");
    check(se_server_send(host.handle, first.event.session_id, binary.data(),
        static_cast<std::uint32_t>(binary.size()), &host.error) < 0, "Raw send must reject HTTP");

    auto corrupt = first.bytes;
    corrupt[28] = 255;
    se_http_request decoded{};
    check(se_http_request_read(corrupt.data(), static_cast<std::uint32_t>(corrupt.size()),
        &decoded, &host.error) == SE_INVALID_ARGUMENT, "Invalid offset/length envelope must fail");
    Bytes unaligned{0};
    unaligned.insert(unaligned.end(), first.bytes.begin(), first.bytes.end());
    success(se_http_request_read(unaligned.data() + 1, static_cast<std::uint32_t>(first.bytes.size()),
        &decoded, &host.error), host.error);
    check(decoded.request_id == first.view.request_id, "HTTP decode must accept unaligned payload");

    se_http_response invalid;
    se_http_response_init(&invalid);
    const se_http_header injection{"X-Test", "okay\r\nInjected: bad"};
    invalid.headers = &injection;
    invalid.header_count = 1;
    check(se_http_respond(host.handle, first.event.session_id, first.view.request_id,
        &invalid, &host.error) == SE_INVALID_ARGUMENT, "HTTP response must reject CRLF injection");
    const se_http_header framing{"Content-Length", "9000"};
    invalid.headers = &framing;
    check(se_http_respond(host.handle, first.event.session_id, first.view.request_id,
        &invalid, &host.error) == SE_INVALID_ARGUMENT, "Application cannot override HTTP framing");

    respond(host, first, binary);
    auto result = read_response(socket);
    check(result.result_int() == 200 && result.body() == binary && result.keep_alive(),
        "Binary HTTP response must preserve bytes and keep the connection alive");
    message.method(http::verb::get);
    message.target("/second");
    message.body().clear();
    message.prepare_payload();
    http::write(socket, message);
    const auto second = next_request(host);
    check(second.event.session_id == first.event.session_id && second.view.request_id != first.view.request_id,
        "Keepalive must retain session but allocate a fresh request ID");
    se_http_response stale;
    se_http_response_init(&stale);
    check(se_http_respond(host.handle, second.event.session_id, first.view.request_id,
        &stale, &host.error) < 0, "Stale response cannot answer a later keepalive request");
    respond(host, second, {}, 204);
    result = read_response(socket);
    check(result.result_int() == 204 && result.body().empty() &&
        result.find(http::field::content_length) == result.end(), "HTTP 204 has no content length or body");

    message.method(http::verb::head);
    http::write(socket, message);
    const auto head = next_request(host);
    respond(host, head, binary);
    result = read_response(socket, true);
    check(result.body().empty() && result[http::field::content_length] == "4",
        "HEAD must preserve representation size without writing its body");
}

void test_rejections(asio::io_context& io)
{
    Host host(io);
    const auto rejected = [&](const std::string& wire, unsigned status) {
        Tcp::socket socket(io);
        socket.connect({asio::ip::address_v4::loopback(), host.port});
        asio::write(socket, asio::buffer(wire));
        const auto response = read_response(socket);
        check(response.result_int() == status && !response.keep_alive(),
            "Invalid HTTP request must receive expected failure and close");
    };
    rejected("GET / HTTP/1.1\r\n\r\n", 400);
    rejected("GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", 400);
    rejected("GET / HTTP/1.1\r\nHost: a\r\nBroken Header\r\n\r\n", 400);
    rejected("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 999999\r\n\r\n", 413);
    rejected("GET / HTTP/1.1\r\nHost: a\r\nX-Large: " + std::string(17000, 'a') + "\r\n\r\n", 431);
    rejected("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\nExpect: 100-continue\r\n\r\n", 417);
    rejected("GET / HTTP/1.0\r\n\r\n", 505);
    rejected("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 4096\r\n\r\n" + std::string(4096, 'a'), 413);
}

void test_chunked_pipeline(asio::io_context& io)
{
    Host host(io);
    Tcp::socket socket(io);
    socket.connect({asio::ip::address_v4::loopback(), host.port});
    const std::string wire = "POST /one HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3\r\none\r\n0\r\n\r\nGET /two HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n";
    asio::write(socket, asio::buffer(wire));
    const auto first = next_request(host);
    check(first.text(first.view.body_offset, first.view.body_size) == "one", "HTTP chunked body must be decoded");
    se_event event;
    se_event_init(&event);
    std::array<std::uint8_t, 4096> bytes{};
    check(se_server_poll_event(host.handle, &event, bytes.data(), static_cast<std::uint32_t>(bytes.size()),
        20, &host.error) == SE_TIMEOUT, "Only one pipelined request may be dispatched at a time");
    respond(host, first, {});
    (void)read_response(socket);
    const auto second = next_request(host);
    check(second.text(second.view.target_offset, second.view.target_size) == "/two",
        "Buffered pipelined HTTP request must not be lost");
    respond(host, second, {});
    check(!read_response(socket).keep_alive(), "Request Connection: close must be respected");
}

void test_response_timeout(asio::io_context& io)
{
    Host host(io, nullptr, 100);
    Tcp::socket socket(io);
    socket.connect({asio::ip::address_v4::loopback(), host.port});
    const std::string wire = "GET /slow HTTP/1.1\r\nHost: a\r\n\r\n";
    asio::write(socket, asio::buffer(wire));
    const auto request = next_request(host);
    bool closed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!closed && std::chrono::steady_clock::now() < deadline) {
        se_event event;
        se_event_init(&event);
        std::array<std::uint8_t, 4096> bytes{};
        const auto status = se_server_poll_event(host.handle, &event, bytes.data(),
            static_cast<std::uint32_t>(bytes.size()), 100, &host.error);
        if (status == SE_TIMEOUT) continue;
        success(status, host.error);
        closed = event.kind == SE_EVENT_CLOSE && event.session_id == request.event.session_id;
    }
    check(closed, "Unanswered HTTP request must close at application timeout");
    se_http_response response;
    se_http_response_init(&response);
    check(se_http_respond(host.handle, request.event.session_id, request.view.request_id,
        &response, &host.error) < 0, "Response after timeout must fail");
}

void test_https(asio::io_context& io)
{
    HttpTestCertificate certificate;
    Host host(io, &certificate);
    asio::ssl::context client(asio::ssl::context::tls_client);
    client.add_certificate_authority(asio::buffer(certificate.certificate_pem));
    client.set_verify_mode(asio::ssl::verify_peer);
    asio::ssl::stream<Tcp::socket> socket(io, client);
    check(SSL_set1_host(socket.native_handle(), "localhost") == 1, "Enable HTTPS hostname verification");
    socket.next_layer().connect({asio::ip::address_v4::loopback(), host.port});
    socket.handshake(asio::ssl::stream_base::client);
    check(SSL_version(socket.native_handle()) == TLS1_3_VERSION, "HTTPS must negotiate TLS 1.3");
    http::request<http::empty_body> request(http::verb::get, "/secure", 11);
    request.set(http::field::host, "localhost");
    http::write(socket, request);
    const auto event = next_request(host);
    const Bytes reply{'s', 'e', 'c', 'u', 'r', 'e'};
    respond(host, event, reply);
    check(read_response(socket).body() == reply, "Verified HTTPS request/response must traverse the DLL ABI");
}

} // namespace

int main()
{
    try {
        check(se_get_abi_version() == SE_ABI_VERSION, "DLL ABI version must match before initializers");
        asio::io_context io;
        test_binary_and_keepalive(io);
        test_rejections(io);
        test_chunked_pipeline(io);
        test_response_timeout(io);
        test_https(io);
        std::cout << "PASS HttpLoopback\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL HttpLoopback: " << error.what() << '\n';
        return 1;
    }
}
