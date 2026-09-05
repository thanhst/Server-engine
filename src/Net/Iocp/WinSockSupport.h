#pragma once

#include <ServerEngine/Net/TcpTypes.h>
#include <ServerEngine/Port/Platform.h>

#if SERVERENGINE_OS_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <string>

namespace serverengine::net::iocp {

// Owns a socket until it is explicitly handed to a Connection or the listener.
class SocketHandle final {
public:
    explicit SocketHandle(SOCKET socket = INVALID_SOCKET) noexcept : socket_(socket) {}
    ~SocketHandle() { reset(); }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    [[nodiscard]] SOCKET get() const noexcept { return socket_; }
    [[nodiscard]] SOCKET release() noexcept;
    void reset(SOCKET socket = INVALID_SOCKET) noexcept;

private:
    SOCKET socket_;
};

[[nodiscard]] SOCKET open_listener(const Endpoint& endpoint, std::string* error_message);
[[nodiscard]] Endpoint remote_endpoint(const sockaddr_in& address);
void configure_client_socket(SOCKET socket, const TcpServerOptions& options);
[[nodiscard]] std::string windows_error_message(DWORD error);
void set_error(std::string* destination, std::string message);

} // namespace serverengine::net::iocp
#endif
