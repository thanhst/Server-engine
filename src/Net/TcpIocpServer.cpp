#include <ServerEngine/Net/TcpIocpServer.h>

#include <ServerEngine/Port/Clock.h>
#include <ServerEngine/Port/Platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if SERVERENGINE_OS_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#else
#include <thread>
#endif

namespace serverengine::net {

namespace {

constexpr int ReceiveBufferSize = 8192;

#if SERVERENGINE_OS_WINDOWS

enum class IoOperation {
    Receive,
    Send
};

struct IocpConnectionState {
    ConnectionId id{};
    SOCKET socket{INVALID_SOCKET};
    Endpoint remote_endpoint{};
    std::atomic_bool closing{false};
    std::atomic<std::uint64_t> last_activity_ms{0};
    std::string pending_message{};
};

struct IoContext {
    OVERLAPPED overlapped{};
    IoOperation operation{IoOperation::Receive};
    std::shared_ptr<IocpConnectionState> connection{};
    WSABUF buffer{};
    std::array<char, ReceiveBufferSize> receive_buffer{};
    std::vector<char> send_buffer{};
};

[[nodiscard]] SOCKET to_socket(port::NativeSocket socket) noexcept
{
    return static_cast<SOCKET>(socket);
}

[[nodiscard]] port::NativeSocket from_socket(SOCKET socket) noexcept
{
    return static_cast<port::NativeSocket>(socket);
}

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

void configure_client_socket(SOCKET socket, const TcpServerOptions& options)
{
    if (options.tcp_no_delay) {
        BOOL enabled = TRUE;
        ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    }
}

[[nodiscard]] std::string windows_error_message(DWORD error)
{
    char* buffer = nullptr;
    const DWORD length = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    if (length == 0 || buffer == nullptr) {
        return "Windows error: " + std::to_string(error);
    }

    std::string message(buffer, length);
    ::LocalFree(buffer);
    return message;
}

#endif

} // namespace

struct TcpIocpServer::ConnectionState
#if SERVERENGINE_OS_WINDOWS
    : IocpConnectionState
#endif
{
#if !SERVERENGINE_OS_WINDOWS
    ConnectionId id{};
#endif
};

TcpIocpServer::TcpIocpServer(core::Logger& logger)
    : logger_(logger)
{
}

TcpIocpServer::~TcpIocpServer()
{
    stop();
}

bool TcpIocpServer::start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message)
{
#if !SERVERENGINE_OS_WINDOWS
    static_cast<void>(options);
    static_cast<void>(callbacks);
    if (error_message != nullptr) {
        *error_message = "IOCP TCP backend is only available on Windows";
    }
    return false;
#else
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

    const auto created_socket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (created_socket == INVALID_SOCKET) {
        if (error_message != nullptr) {
            *error_message = "WSASocketW() failed: " + port::socket_error_message();
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

    completion_port_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (completion_port_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "CreateIoCompletionPort() failed: " + windows_error_message(::GetLastError());
        }
        port::close_socket(listen_socket_);
        listen_socket_ = port::InvalidSocket;
        return false;
    }

    options_ = std::move(options);
    callbacks_ = std::move(callbacks);
    running_ = true;

    const auto worker_count = std::max<std::size_t>(1, options_.worker_count);
    worker_threads_.reserve(worker_count);
    for (std::size_t index = 0; index < worker_count; ++index) {
        worker_threads_.emplace_back(&TcpIocpServer::worker_loop, this);
    }

    accept_thread_ = std::thread(&TcpIocpServer::accept_loop, this);
    idle_monitor_thread_ = std::thread(&TcpIocpServer::idle_monitor_loop, this);

    logger_.info("TCP IOCP listening on ", to_string(options_.bind_endpoint), " workers=", worker_count);
    return true;
#endif
}

void TcpIocpServer::stop()
{
#if SERVERENGINE_OS_WINDOWS
    if (!running_) {
        return;
    }

    running_ = false;

    if (port::is_valid_socket(listen_socket_)) {
        port::close_socket(listen_socket_);
        listen_socket_ = port::InvalidSocket;
    }

    close_all_connections(true);

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    if (idle_monitor_thread_.joinable()) {
        idle_monitor_thread_.join();
    }

    if (completion_port_ != nullptr) {
        for (std::size_t index = 0; index < worker_threads_.size(); ++index) {
            ::PostQueuedCompletionStatus(static_cast<HANDLE>(completion_port_), 0, 0, nullptr);
        }
    }

    for (auto& worker : worker_threads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    worker_threads_.clear();

    if (completion_port_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(completion_port_));
        completion_port_ = nullptr;
    }

    logger_.info("TCP IOCP listener stopped on ", to_string(options_.bind_endpoint));
#endif
}

bool TcpIocpServer::is_running() const noexcept
{
    return running_;
}

bool TcpIocpServer::send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message)
{
#if !SERVERENGINE_OS_WINDOWS
    static_cast<void>(connection_id);
    static_cast<void>(data);
    if (error_message != nullptr) {
        *error_message = "IOCP TCP backend is only available on Windows";
    }
    return false;
#else
    if (data.empty()) {
        return true;
    }

    std::shared_ptr<ConnectionState> connection;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        const auto iterator = connections_.find(connection_id);
        if (iterator == connections_.end()) {
            if (error_message != nullptr) {
                *error_message = "connection not found";
            }
            return false;
        }
        connection = iterator->second;
    }

    if (connection->closing.load()) {
        if (error_message != nullptr) {
            *error_message = "connection is closing";
        }
        return false;
    }

    auto context = std::make_unique<IoContext>();
    context->operation = IoOperation::Send;
    context->connection = connection;
    context->send_buffer.assign(reinterpret_cast<const char*>(data.data()), reinterpret_cast<const char*>(data.data()) + data.size());
    context->buffer.buf = context->send_buffer.data();
    context->buffer.len = static_cast<ULONG>(context->send_buffer.size());

    DWORD bytes_sent = 0;
    const int result = ::WSASend(connection->socket, &context->buffer, 1, &bytes_sent, 0, &context->overlapped, nullptr);
    if (result == SOCKET_ERROR) {
        const auto error = ::WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            if (error_message != nullptr) {
                *error_message = "WSASend() failed: " + port::socket_error_message();
            }
            close_connection(connection_id, true);
            return false;
        }
    }

    static_cast<void>(context.release());
    return true;
#endif
}

void TcpIocpServer::disconnect(ConnectionId connection_id) noexcept
{
    close_connection(connection_id, true);
}

void TcpIocpServer::accept_loop()
{
#if SERVERENGINE_OS_WINDOWS
    while (running_) {
        sockaddr_in remote_address{};
        int remote_size = sizeof(remote_address);
        const auto accepted_socket = ::accept(
            to_socket(listen_socket_),
            reinterpret_cast<sockaddr*>(&remote_address),
            &remote_size);

        if (accepted_socket == INVALID_SOCKET) {
            if (running_) {
                report_error("accept() failed: " + port::socket_error_message());
            }
            continue;
        }

        const auto connection_id = next_connection_id_.fetch_add(1);
        Endpoint remote_endpoint{
            endpoint_from_sockaddr(remote_address),
            ntohs(remote_address.sin_port)
        };

        if (callbacks_.on_accepting && !callbacks_.on_accepting(connection_id, remote_endpoint)) {
            ::closesocket(accepted_socket);
            continue;
        }

        configure_client_socket(accepted_socket, options_);

        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            if (connections_.size() >= options_.max_connections) {
                ::closesocket(accepted_socket);
                report_error("connection rejected: max TCP IOCP connections reached");
                continue;
            }
        }

        auto connection = std::make_shared<ConnectionState>();
        connection->id = connection_id;
        connection->socket = accepted_socket;
        connection->remote_endpoint = remote_endpoint;
        connection->last_activity_ms = port::steady_milliseconds();

        const HANDLE association = ::CreateIoCompletionPort(
            reinterpret_cast<HANDLE>(accepted_socket),
            static_cast<HANDLE>(completion_port_),
            reinterpret_cast<ULONG_PTR>(connection.get()),
            0);

        if (association == nullptr) {
            report_error("CreateIoCompletionPort(client) failed: " + windows_error_message(::GetLastError()));
            ::closesocket(accepted_socket);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_[connection_id] = connection;
        }

        if (callbacks_.on_connected) {
            callbacks_.on_connected(connection_id, remote_endpoint);
        }

        auto context = std::make_unique<IoContext>();
        context->operation = IoOperation::Receive;
        context->connection = connection;
        context->buffer.buf = context->receive_buffer.data();
        context->buffer.len = static_cast<ULONG>(context->receive_buffer.size());

        DWORD flags = 0;
        DWORD received = 0;
        const int result = ::WSARecv(accepted_socket, &context->buffer, 1, &received, &flags, &context->overlapped, nullptr);
        if (result == SOCKET_ERROR) {
            const auto error = ::WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                report_error("WSARecv() failed: " + port::socket_error_message());
                close_connection(connection_id, true);
                continue;
            }
        }

        static_cast<void>(context.release());
    }
#endif
}

void TcpIocpServer::worker_loop()
{
#if SERVERENGINE_OS_WINDOWS
    while (running_) {
        DWORD transferred = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED* overlapped = nullptr;

        const BOOL ok = ::GetQueuedCompletionStatus(
            static_cast<HANDLE>(completion_port_),
            &transferred,
            &completion_key,
            &overlapped,
            INFINITE);

        static_cast<void>(completion_key);

        if (overlapped == nullptr) {
            break;
        }

        std::unique_ptr<IoContext> context(reinterpret_cast<IoContext*>(overlapped));
        auto connection = context->connection;
        if (!ok || transferred == 0 || connection == nullptr || connection->closing.load()) {
            if (connection != nullptr) {
                close_connection(connection->id, true);
            }
            continue;
        }

        if (context->operation == IoOperation::Send) {
            connection->last_activity_ms = port::steady_milliseconds();
            continue;
        }

        connection->last_activity_ms = port::steady_milliseconds();
        connection->pending_message.append(context->receive_buffer.data(), static_cast<std::size_t>(transferred));

        bool should_close = false;
        for (;;) {
            const auto newline = connection->pending_message.find('\n');
            if (newline == std::string::npos) {
                break;
            }

            if (newline > options_.max_message_bytes) {
                report_error("connection closed: message exceeds max_message_bytes");
                should_close = true;
                break;
            }

            auto line = connection->pending_message.substr(0, newline);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            connection->pending_message.erase(0, newline + 1);

            if (callbacks_.on_message) {
                callbacks_.on_message(connection->id, connection->remote_endpoint, core::Buffer::from_text(line));
            }

            if (connection->closing.load()) {
                should_close = true;
                break;
            }
        }

        if (!should_close && connection->pending_message.size() > options_.max_message_bytes) {
            report_error("connection closed: pending message exceeds max_message_bytes");
            should_close = true;
        }

        if (should_close) {
            close_connection(connection->id, true);
            continue;
        }

        auto receive_context = std::make_unique<IoContext>();
        receive_context->operation = IoOperation::Receive;
        receive_context->connection = connection;
        receive_context->buffer.buf = receive_context->receive_buffer.data();
        receive_context->buffer.len = static_cast<ULONG>(receive_context->receive_buffer.size());

        DWORD flags = 0;
        DWORD received = 0;
        const int result = ::WSARecv(
            connection->socket,
            &receive_context->buffer,
            1,
            &received,
            &flags,
            &receive_context->overlapped,
            nullptr);

        if (result == SOCKET_ERROR) {
            const auto error = ::WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                report_error("WSARecv() failed: " + port::socket_error_message());
                close_connection(connection->id, true);
                continue;
            }
        }

        static_cast<void>(receive_context.release());
    }
#endif
}

void TcpIocpServer::idle_monitor_loop()
{
#if SERVERENGINE_OS_WINDOWS
    while (running_) {
        port::sleep_for(std::chrono::milliseconds(1000));

        if (options_.idle_timeout_ms == 0) {
            continue;
        }

        std::vector<std::shared_ptr<ConnectionState>> snapshot;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            snapshot.reserve(connections_.size());
            for (const auto& [id, connection] : connections_) {
                static_cast<void>(id);
                snapshot.push_back(connection);
            }
        }

        const auto now_ms = port::steady_milliseconds();
        for (const auto& connection : snapshot) {
            if (connection == nullptr || connection->closing.load()) {
                continue;
            }

            const auto last_activity_ms = connection->last_activity_ms.load();
            if (last_activity_ms > 0 && now_ms - last_activity_ms >= options_.idle_timeout_ms) {
                logger_.info("TCP IOCP idle timeout connection=", connection->id, " remote=", to_string(connection->remote_endpoint));
                close_connection(connection->id, true);
            }
        }
    }
#endif
}

void TcpIocpServer::close_connection(ConnectionId connection_id, bool notify) noexcept
{
#if SERVERENGINE_OS_WINDOWS
    std::shared_ptr<ConnectionState> connection;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        const auto iterator = connections_.find(connection_id);
        if (iterator == connections_.end()) {
            return;
        }

        connection = iterator->second;
        connections_.erase(iterator);
    }

    if (connection == nullptr) {
        return;
    }

    const bool was_closing = connection->closing.exchange(true);
    if (!was_closing && connection->socket != INVALID_SOCKET) {
        ::shutdown(connection->socket, SD_BOTH);
        ::closesocket(connection->socket);
        connection->socket = INVALID_SOCKET;
    }

    if (notify && callbacks_.on_disconnected) {
        callbacks_.on_disconnected(connection->id, connection->remote_endpoint);
    }
#else
    static_cast<void>(connection_id);
    static_cast<void>(notify);
#endif
}

void TcpIocpServer::close_all_connections(bool notify) noexcept
{
#if SERVERENGINE_OS_WINDOWS
    std::vector<ConnectionId> ids;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        ids.reserve(connections_.size());
        for (const auto& [id, connection] : connections_) {
            static_cast<void>(connection);
            ids.push_back(id);
        }
    }

    for (const auto id : ids) {
        close_connection(id, notify);
    }
#else
    static_cast<void>(notify);
#endif
}

void TcpIocpServer::report_error(std::string_view message)
{
    logger_.error("TCP IOCP error: ", message);
    if (callbacks_.on_error) {
        callbacks_.on_error(message);
    }
}

} // namespace serverengine::net
