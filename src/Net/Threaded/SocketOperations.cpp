#include "SocketOperations.h"

#include <algorithm>

#if SERVERENGINE_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

namespace serverengine::net::threaded {

namespace {

#if SERVERENGINE_OS_WINDOWS
[[nodiscard]] SOCKET native_socket(port::NativeSocket socket) noexcept
{
    return static_cast<SOCKET>(socket);
}
#else
[[nodiscard]] int native_socket(port::NativeSocket socket) noexcept
{
    return socket;
}
#endif

[[nodiscard]] bool set_nonblocking(port::NativeSocket socket, bool enabled)
{
#if SERVERENGINE_OS_WINDOWS
    u_long mode = enabled ? 1 : 0;
    return ::ioctlsocket(native_socket(socket), FIONBIO, &mode) == 0;
#else
    const int flags = ::fcntl(native_socket(socket), F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(native_socket(socket), F_SETFL, enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) == 0;
#endif
}

} // namespace

port::NativeSocket open_listener(const Endpoint& endpoint, std::string* error_message)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1) {
        if (error_message != nullptr) {
            *error_message = "invalid IPv4 bind address: " + endpoint.address;
        }
        return port::InvalidSocket;
    }

    const auto socket = static_cast<port::NativeSocket>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!port::is_valid_socket(socket)) {
        if (error_message != nullptr) {
            *error_message = "socket() failed: " + port::socket_error_message();
        }
        return port::InvalidSocket;
    }

    int reuse_address = 1;
    ::setsockopt(native_socket(socket), SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse_address), sizeof(reuse_address));

    if (::bind(native_socket(socket), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        if (error_message != nullptr) {
            *error_message = "bind() failed: " + port::socket_error_message();
        }
        port::close_socket(socket);
        return port::InvalidSocket;
    }
    if (::listen(native_socket(socket), SOMAXCONN) != 0) {
        if (error_message != nullptr) {
            *error_message = "listen() failed: " + port::socket_error_message();
        }
        port::close_socket(socket);
        return port::InvalidSocket;
    }
    if (!set_nonblocking(socket, true)) {
        if (error_message != nullptr) {
            *error_message = "setting listener nonblocking failed: " + port::socket_error_message();
        }
        port::close_socket(socket);
        return port::InvalidSocket;
    }
    return socket;
}

int wait_for_client(port::NativeSocket listener)
{
#if SERVERENGINE_OS_WINDOWS
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(native_socket(listener), &readable);
    timeval timeout{0, 100000};
    return ::select(0, &readable, nullptr, nullptr, &timeout);
#else
    // poll also handles descriptor values above select's FD_SETSIZE limit.
    pollfd descriptor{};
    descriptor.fd = native_socket(listener);
    descriptor.events = POLLIN;
    return ::poll(&descriptor, 1, 100);
#endif
}

port::NativeSocket accept_client(port::NativeSocket listener, Endpoint& remote_endpoint)
{
    sockaddr_in address{};
#if SERVERENGINE_OS_WINDOWS
    int address_size = sizeof(address);
#else
    socklen_t address_size = sizeof(address);
#endif
    const auto socket = static_cast<port::NativeSocket>(::accept(
        native_socket(listener), reinterpret_cast<sockaddr*>(&address), &address_size));
    if (!port::is_valid_socket(socket)) {
        return port::InvalidSocket;
    }

    char buffer[INET_ADDRSTRLEN]{};
    const auto* converted = ::inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
    remote_endpoint = Endpoint{converted == nullptr ? "0.0.0.0" : buffer, ntohs(address.sin_port)};
    return socket;
}

bool last_accept_would_block() noexcept
{
#if SERVERENGINE_OS_WINDOWS
    return ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

bool configure_client_socket(port::NativeSocket socket, const TcpServerOptions& options, std::string* error_message)
{
    // Accepted sockets can inherit the listener's mode. This backend uses
    // blocking client I/O even though its listener must never block in accept.
    if (!set_nonblocking(socket, false)) {
        if (error_message != nullptr) {
            *error_message = "setting client blocking failed: " + port::socket_error_message();
        }
        return false;
    }
    const int timeout_ms = (std::max)(1, options.receive_timeout_ms);
#if SERVERENGINE_OS_WINDOWS
    const auto timeout = static_cast<DWORD>(timeout_ms);
    ::setsockopt(native_socket(socket), SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(native_socket(socket), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    // BSD-derived platforms configure this per socket instead of per send.
    int no_sigpipe = 1;
    ::setsockopt(native_socket(socket), SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
#endif
    if (options.tcp_no_delay) {
        int enabled = 1;
        ::setsockopt(native_socket(socket), IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    }
    return true;
}

int receive_socket(port::NativeSocket socket, char* buffer, int size)
{
#if SERVERENGINE_OS_WINDOWS
    return ::recv(native_socket(socket), buffer, size, 0);
#else
    return static_cast<int>(::recv(native_socket(socket), buffer, static_cast<std::size_t>(size), 0));
#endif
}

int send_socket(port::NativeSocket socket, const char* buffer, int size)
{
#if SERVERENGINE_OS_WINDOWS
    return ::send(native_socket(socket), buffer, size, 0);
#else
    // A closed peer must become a send error, not terminate the process.
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    return static_cast<int>(::send(native_socket(socket), buffer, static_cast<std::size_t>(size), flags));
#endif
}

bool last_receive_was_timeout() noexcept
{
#if SERVERENGINE_OS_WINDOWS
    return ::WSAGetLastError() == WSAETIMEDOUT;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void shutdown_socket(port::NativeSocket socket) noexcept
{
    if (!port::is_valid_socket(socket)) {
        return;
    }
#if SERVERENGINE_OS_WINDOWS
    ::shutdown(native_socket(socket), SD_BOTH);
#else
    ::shutdown(native_socket(socket), SHUT_RDWR);
#endif
}

} // namespace serverengine::net::threaded
