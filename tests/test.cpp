#include <ServerEngine/Port/Socket.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#if SERVERENGINE_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

namespace {

struct TestOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    std::string user{"thanh"};
    std::string token{"secret"};
    std::string message{"A"};
    int timeout_ms{3000};
};

#if SERVERENGINE_OS_WINDOWS
using SocketHandle = SOCKET;

[[nodiscard]] SocketHandle to_socket(serverengine::port::NativeSocket socket) noexcept
{
    return static_cast<SOCKET>(socket);
}

[[nodiscard]] serverengine::port::NativeSocket from_socket(SocketHandle socket) noexcept
{
    return static_cast<serverengine::port::NativeSocket>(socket);
}

#else
using SocketHandle = int;

[[nodiscard]] SocketHandle to_socket(serverengine::port::NativeSocket socket) noexcept
{
    return socket;
}

[[nodiscard]] serverengine::port::NativeSocket from_socket(SocketHandle socket) noexcept
{
    return socket;
}

#endif

[[nodiscard]] std::uint16_t parse_port(const char* value)
{
    const int parsed = std::atoi(value);
    if (parsed <= 0 || parsed > 65535) {
        throw std::runtime_error("invalid port");
    }

    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] TestOptions parse_args(int argc, char** argv)
{
    TestOptions options;
    if (argc > 1) {
        options.host = argv[1];
    }
    if (argc > 2) {
        options.port = parse_port(argv[2]);
    }
    if (argc > 3) {
        options.message = argv[3];
    }
    if (argc > 4) {
        options.user = argv[4];
    }
    if (argc > 5) {
        options.token = argv[5];
    }

    return options;
}

void set_timeouts(serverengine::port::NativeSocket socket, int timeout_ms)
{
#if SERVERENGINE_OS_WINDOWS
    const auto timeout = static_cast<DWORD>(timeout_ms);
    ::setsockopt(to_socket(socket), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    ::setsockopt(to_socket(socket), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(to_socket(socket), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(to_socket(socket), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

void connect_to(serverengine::port::NativeSocket socket, const TestOptions& options)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);

    if (::inet_pton(AF_INET, options.host.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid IPv4 host: " + options.host);
    }

    if (::connect(to_socket(socket), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw std::runtime_error("connect failed: " + serverengine::port::socket_error_message());
    }
}

void send_all(serverengine::port::NativeSocket socket, std::string_view text)
{
    auto* cursor = text.data();
    auto remaining = static_cast<int>(text.size());

    while (remaining > 0) {
        const int sent = ::send(to_socket(socket), cursor, remaining, 0);
        if (sent <= 0) {
            throw std::runtime_error("send failed: " + serverengine::port::socket_error_message());
        }

        cursor += sent;
        remaining -= sent;
    }
}

[[nodiscard]] std::string read_line(serverengine::port::NativeSocket socket)
{
    std::string line;

    for (;;) {
        char ch = 0;
        const int received = ::recv(to_socket(socket), &ch, 1, 0);
        if (received <= 0) {
            throw std::runtime_error("recv failed: " + serverengine::port::socket_error_message());
        }

        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }

        line.push_back(ch);
    }
}

void expect_equal(std::string_view name, std::string_view actual, std::string_view expected)
{
    if (actual != expected) {
        throw std::runtime_error(std::string(name) + " expected '" + std::string(expected) + "' but got '" + std::string(actual) + "'");
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto options = parse_args(argc, argv);

        serverengine::port::SocketSystem socket_system;
        if (!socket_system.initialized()) {
            throw std::runtime_error(socket_system.error_message());
        }

        const auto created_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (
#if SERVERENGINE_OS_WINDOWS
            created_socket == INVALID_SOCKET
#else
            created_socket < 0
#endif
        ) {
            throw std::runtime_error("socket failed: " + serverengine::port::socket_error_message());
        }

        const auto socket = from_socket(created_socket);
        set_timeouts(socket, options.timeout_ms);
        connect_to(socket, options);

        const auto hello = read_line(socket);
        send_all(socket, "AUTH " + options.user + " " + options.token + "\n");
        const auto auth = read_line(socket);
        send_all(socket, options.message + "\n");
        const auto echo = read_line(socket);
        send_all(socket, "WHO\n");
        const auto who_header = read_line(socket);
        const auto who_line = read_line(socket);

        serverengine::port::close_socket(socket);

        expect_equal("hello", hello, "AUTH REQUIRED");
        expect_equal("auth", auth, "AUTH OK");
        expect_equal("echo", echo, "echo: " + options.message);

        if (who_header.rfind("WHO count=", 0) != 0) {
            throw std::runtime_error("WHO header is invalid: " + who_header);
        }

        std::cout << "Connected: " << options.host << ':' << options.port << '\n';
        std::cout << "Hello: " << hello << '\n';
        std::cout << "Auth: " << auth << '\n';
        std::cout << "Echo: " << echo << '\n';
        std::cout << "WhoHeader: " << who_header << '\n';
        std::cout << "WhoLine: " << who_line << '\n';
        std::cout << "PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
