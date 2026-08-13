#include <ServerEngine/Net/TcpServer.h>

#include <ServerEngine/Port/Clock.h>
#include <ServerEngine/Port/Platform.h>

#include <array>
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#if SERVERENGINE_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

namespace serverengine::net {

namespace {

constexpr int ReceiveBufferSize = 4096;

#if SERVERENGINE_OS_WINDOWS

[[nodiscard]] SOCKET to_socket(port::NativeSocket socket) noexcept
{
    return static_cast<SOCKET>(socket);
}

[[nodiscard]] port::NativeSocket from_socket(SOCKET socket) noexcept
{
    return static_cast<port::NativeSocket>(socket);
}

[[nodiscard]] int receive_socket(port::NativeSocket socket, char* buffer, int size)
{
    return ::recv(to_socket(socket), buffer, size, 0);
}

[[nodiscard]] int send_socket(port::NativeSocket socket, const char* buffer, int size)
{
    return ::send(to_socket(socket), buffer, size, 0);
}

[[nodiscard]] bool last_receive_was_timeout()
{
    return ::WSAGetLastError() == WSAETIMEDOUT;
}

void configure_client_socket(port::NativeSocket socket, const TcpServerOptions& options)
{
    const auto receive_timeout = static_cast<DWORD>(std::max(1, options.receive_timeout_ms));
    ::setsockopt(to_socket(socket), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_timeout), sizeof(receive_timeout));

    if (options.tcp_no_delay) {
        BOOL enabled = TRUE;
        ::setsockopt(to_socket(socket), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    }
}

#else

[[nodiscard]] int to_socket(port::NativeSocket socket) noexcept
{
    return socket;
}

[[nodiscard]] port::NativeSocket from_socket(int socket) noexcept
{
    return socket;
}

[[nodiscard]] int receive_socket(port::NativeSocket socket, char* buffer, int size)
{
    return static_cast<int>(::recv(to_socket(socket), buffer, static_cast<std::size_t>(size), 0));
}

[[nodiscard]] int send_socket(port::NativeSocket socket, const char* buffer, int size)
{
    return static_cast<int>(::send(to_socket(socket), buffer, static_cast<std::size_t>(size), 0));
}

[[nodiscard]] bool last_receive_was_timeout()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

void configure_client_socket(port::NativeSocket socket, const TcpServerOptions& options)
{
    timeval timeout{};
    timeout.tv_sec = std::max(1, options.receive_timeout_ms) / 1000;
    timeout.tv_usec = (std::max(1, options.receive_timeout_ms) % 1000) * 1000;
    ::setsockopt(to_socket(socket), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (options.tcp_no_delay) {
        int enabled = 1;
        ::setsockopt(to_socket(socket), IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
    }
}

#endif

[[nodiscard]] std::string endpoint_from_sockaddr(const sockaddr_in& address)
{
    char buffer[INET_ADDRSTRLEN] = {};
    const auto* result = ::inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
    if (result == nullptr) {
        return "0.0.0.0";
    }

    return std::string(buffer);
}

[[nodiscard]] bool make_sockaddr(const Endpoint& endpoint, sockaddr_in& address, std::string* error_message)
{
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);

    if (::inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1) {
        if (error_message != nullptr) {
            *error_message = "invalid IPv4 bind address: " + endpoint.address;
        }
        return false;
    }

    return true;
}

} // namespace

TcpServer::TcpServer(core::Logger& logger)
    : logger_(logger)
{
}

TcpServer::~TcpServer()
{
    stop();
}

bool TcpServer::start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message)
{
    if (running_) {
        return true;
    }

    if (!socket_system_.initialized()) {
        if (error_message != nullptr) {
            *error_message = socket_system_.error_message();
        }
        return false;
    }

    sockaddr_in bind_address{};
    if (!make_sockaddr(options.bind_endpoint, bind_address, error_message)) {
        return false;
    }

    const auto created_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (
#if SERVERENGINE_OS_WINDOWS
        created_socket == INVALID_SOCKET
#else
        created_socket < 0
#endif
    ) {
        if (error_message != nullptr) {
            *error_message = "socket() failed: " + port::socket_error_message();
        }
        return false;
    }

    listen_socket_ = from_socket(created_socket);

    int reuse_address = 1;
    ::setsockopt(
        to_socket(listen_socket_),
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse_address),
        sizeof(reuse_address));

    if (::bind(to_socket(listen_socket_), reinterpret_cast<const sockaddr*>(&bind_address), sizeof(bind_address)) != 0) {
        if (error_message != nullptr) {
            *error_message = "bind() failed: " + port::socket_error_message();
        }
        port::close_socket(listen_socket_);
        listen_socket_ = port::InvalidSocket;
        return false;
    }

    if (::listen(to_socket(listen_socket_), SOMAXCONN) != 0) {
        if (error_message != nullptr) {
            *error_message = "listen() failed: " + port::socket_error_message();
        }
        port::close_socket(listen_socket_);
        listen_socket_ = port::InvalidSocket;
        return false;
    }

    options_ = std::move(options);
    callbacks_ = std::move(callbacks);
    running_ = true;
    accept_thread_ = std::thread(&TcpServer::accept_loop, this);

    logger_.info("TCP listening on ", to_string(options_.bind_endpoint));
    return true;
}

void TcpServer::stop()
{
    if (!running_) {
        return;
    }

    running_ = false;

    if (port::is_valid_socket(listen_socket_)) {
        port::close_socket(listen_socket_);
        listen_socket_ = port::InvalidSocket;
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto& [connection_id, socket] : clients_) {
            static_cast<void>(connection_id);
            port::close_socket(socket);
        }
        clients_.clear();
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    for (auto& thread : client_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    client_threads_.clear();

    logger_.info("TCP listener stopped on ", to_string(options_.bind_endpoint));
}

bool TcpServer::is_running() const noexcept
{
    return running_;
}

bool TcpServer::send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message)
{
    port::NativeSocket socket = port::InvalidSocket;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        const auto iterator = clients_.find(connection_id);
        if (iterator == clients_.end()) {
            if (error_message != nullptr) {
                *error_message = "connection not found";
            }
            return false;
        }
        socket = iterator->second;
    }

    const char* cursor = reinterpret_cast<const char*>(data.data());
    auto remaining = static_cast<int>(data.size());

    while (remaining > 0) {
        const int sent = send_socket(socket, cursor, remaining);
        if (sent <= 0) {
            if (error_message != nullptr) {
                *error_message = "send() failed: " + port::socket_error_message();
            }
            return false;
        }

        cursor += sent;
        remaining -= sent;
    }

    return true;
}

void TcpServer::disconnect(ConnectionId connection_id) noexcept
{
    close_client(connection_id);
}

void TcpServer::accept_loop()
{
    while (running_) {
        sockaddr_in remote_address{};
#if SERVERENGINE_OS_WINDOWS
        int remote_size = sizeof(remote_address);
#else
        socklen_t remote_size = sizeof(remote_address);
#endif

        const auto accepted_socket = ::accept(
            to_socket(listen_socket_),
            reinterpret_cast<sockaddr*>(&remote_address),
            &remote_size);

        if (
#if SERVERENGINE_OS_WINDOWS
            accepted_socket == INVALID_SOCKET
#else
            accepted_socket < 0
#endif
        ) {
            if (running_) {
                report_error("accept() failed: " + port::socket_error_message());
            }
            continue;
        }

        const auto client_socket = from_socket(accepted_socket);
        const auto connection_id = next_connection_id_.fetch_add(1);
        Endpoint remote_endpoint{
            endpoint_from_sockaddr(remote_address),
            ntohs(remote_address.sin_port)
        };

        if (callbacks_.on_accepting && !callbacks_.on_accepting(connection_id, remote_endpoint)) {
            port::close_socket(client_socket);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            if (clients_.size() >= options_.max_connections) {
                port::close_socket(client_socket);
                report_error("connection rejected: max TCP connections reached");
                continue;
            }

            clients_[connection_id] = client_socket;
            configure_client_socket(client_socket, options_);
            client_threads_.emplace_back(&TcpServer::client_loop, this, connection_id, client_socket, remote_endpoint);
        }

        if (callbacks_.on_connected) {
            callbacks_.on_connected(connection_id, remote_endpoint);
        }
    }
}

void TcpServer::client_loop(ConnectionId connection_id, port::NativeSocket client_socket, Endpoint remote_endpoint)
{
    std::array<char, ReceiveBufferSize> receive_buffer{};
    std::string pending_message;
    auto last_activity_ms = port::steady_milliseconds();

    while (running_) {
        const int received = receive_socket(client_socket, receive_buffer.data(), static_cast<int>(receive_buffer.size()));
        if (received > 0) {
            last_activity_ms = port::steady_milliseconds();
            pending_message.append(receive_buffer.data(), static_cast<std::size_t>(received));

            if (pending_message.size() > options_.max_message_bytes) {
                report_error("connection closed: message exceeds max_message_bytes");
                break;
            }

            for (;;) {
                const auto newline = pending_message.find('\n');
                if (newline == std::string::npos) {
                    break;
                }

                auto line = pending_message.substr(0, newline);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                pending_message.erase(0, newline + 1);

                if (callbacks_.on_message) {
                    callbacks_.on_message(connection_id, remote_endpoint, core::Buffer::from_text(line));
                }
            }
            continue;
        }

        if (received < 0 && running_) {
            if (last_receive_was_timeout()) {
                const auto now_ms = port::steady_milliseconds();
                if (options_.idle_timeout_ms > 0 && now_ms - last_activity_ms >= options_.idle_timeout_ms) {
                    logger_.info("TCP idle timeout connection=", connection_id, " remote=", to_string(remote_endpoint));
                    break;
                }
                continue;
            }

            report_error("recv() failed: " + port::socket_error_message());
        }
        break;
    }

    close_client(connection_id);

    if (callbacks_.on_disconnected) {
        callbacks_.on_disconnected(connection_id, remote_endpoint);
    }
}

void TcpServer::close_client(ConnectionId connection_id) noexcept
{
    port::NativeSocket socket = port::InvalidSocket;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        const auto iterator = clients_.find(connection_id);
        if (iterator == clients_.end()) {
            return;
        }

        socket = iterator->second;
        clients_.erase(iterator);
    }

    port::close_socket(socket);
}

void TcpServer::report_error(std::string_view message)
{
    logger_.error("TCP error: ", message);
    if (callbacks_.on_error) {
        callbacks_.on_error(message);
    }
}

} // namespace serverengine::net
