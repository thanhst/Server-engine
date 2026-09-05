#pragma once

#include <ServerEngine/Port/Platform.h>

#if SERVERENGINE_OS_WINDOWS
#include "CompletionPort.h"
#include "Connection.h"

#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Port/Socket.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace serverengine::net::iocp {

// Coordinates listener, connections and callbacks. Connection owns socket I/O;
// CompletionPort owns completion delivery and the shutdown drain barrier.
// Lifecycle calls belong to the owner thread, never to a transport callback.
class Server final {
public:
    explicit Server(core::Logger& logger);
    ~Server();

    [[nodiscard]] bool start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message);
    void stop();
    [[nodiscard]] bool is_running() const noexcept { return running_; }
    [[nodiscard]] bool send(ConnectionId id, const core::Buffer& data, std::string* error_message);
    void disconnect(ConnectionId id) noexcept;

private:
    class CallbackScope final {
    public:
        CallbackScope(Server& server, const std::shared_ptr<Connection>& connection);
        ~CallbackScope();
        CallbackScope(const CallbackScope&) = delete;
        CallbackScope& operator=(const CallbackScope&) = delete;
        [[nodiscard]] explicit operator bool() const noexcept { return entered_; }

    private:
        Server& server_;
        std::shared_ptr<Connection> connection_;
        bool entered_;
    };

    void accept_loop();
    void worker_loop();
    void idle_monitor_loop();
    void deliver_received(const std::shared_ptr<Connection>& connection, std::string_view bytes);
    void close_all_connections() noexcept;
    void notify_disconnected(const std::shared_ptr<Connection>& connection);
    void report_error(std::string_view message);
    [[nodiscard]] std::vector<std::shared_ptr<Connection>> connection_snapshot();

    core::Logger& logger_;
    port::SocketSystem socket_system_;
    TcpServerOptions options_{};
    TcpServerCallbacks callbacks_{};
    CompletionPort completion_port_;
    // Assigned before accept starts; closed only after the accept thread joins.
    SOCKET listen_socket_{INVALID_SOCKET};
    std::atomic_bool running_{false};
    std::atomic<ConnectionId> next_connection_id_{1};
    std::thread accept_thread_;
    std::thread idle_monitor_thread_;
    std::vector<std::thread> worker_threads_;
    std::mutex connections_mutex_;
    std::unordered_map<ConnectionId, std::shared_ptr<Connection>> connections_;
};

} // namespace serverengine::net::iocp
#else
namespace serverengine::net::iocp {
// Completes the facade's unique_ptr type on platforms without IOCP.
class Server final {};
} // namespace serverengine::net::iocp
#endif
