#include "WinSockSupport.h"

#if SERVERENGINE_OS_WINDOWS
#include <ServerEngine/Port/Socket.h>

#include <utility>

namespace serverengine::net::iocp {

SOCKET SocketHandle::release() noexcept
{
    return std::exchange(socket_, INVALID_SOCKET);
}

void SocketHandle::reset(SOCKET socket) noexcept
{
    if (socket_ != INVALID_SOCKET) {
        ::closesocket(socket_);
    }
    socket_ = socket;
}

void set_error(std::string* destination, std::string message)
{
    if (destination != nullptr) {
        *destination = std::move(message);
    }
}

SOCKET open_listener(const Endpoint& endpoint, std::string* error_message)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1) {
        set_error(error_message, "invalid IPv4 bind address: " + endpoint.address);
        return INVALID_SOCKET;
    }

    SocketHandle socket(::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED));
    if (socket.get() == INVALID_SOCKET) {
        set_error(error_message, "WSASocketW() failed: " + port::socket_error_message());
        return INVALID_SOCKET;
    }

    int reuse_address = 1;
    ::setsockopt(socket.get(), SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse_address), sizeof(reuse_address));

    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        set_error(error_message, "bind() failed: " + port::socket_error_message());
        return INVALID_SOCKET;
    }
    if (::listen(socket.get(), SOMAXCONN) != 0) {
        set_error(error_message, "listen() failed: " + port::socket_error_message());
        return INVALID_SOCKET;
    }
    // A bounded readiness wait lets the accept thread observe stop without
    // another thread closing a socket while accept is still using it.
    u_long nonblocking = 1;
    if (::ioctlsocket(socket.get(), FIONBIO, &nonblocking) != 0) {
        set_error(error_message, "ioctlsocket(FIONBIO) failed: " + port::socket_error_message());
        return INVALID_SOCKET;
    }
    return socket.release();
}

Endpoint remote_endpoint(const sockaddr_in& address)
{
    char buffer[INET_ADDRSTRLEN]{};
    const char* text = ::inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
    return {text != nullptr ? text : "0.0.0.0", ntohs(address.sin_port)};
}

void configure_client_socket(SOCKET socket, const TcpServerOptions& options)
{
    if (options.tcp_no_delay) {
        BOOL enabled = TRUE;
        ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    }
}

std::string windows_error_message(DWORD error)
{
    char* buffer = nullptr;
    const DWORD length = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        return "Windows error: " + std::to_string(error);
    }
    std::string message(buffer, length);
    ::LocalFree(buffer);
    return message;
}

} // namespace serverengine::net::iocp
#endif
