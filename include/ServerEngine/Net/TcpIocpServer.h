#pragma once

#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Net/ITcpServer.h>
#include <ServerEngine/Port/Socket.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace serverengine::net {

class TcpIocpServer final : public ITcpServer {
public:
    explicit TcpIocpServer(core::Logger& logger);
    ~TcpIocpServer() override;

    TcpIocpServer(const TcpIocpServer&) = delete;
    TcpIocpServer& operator=(const TcpIocpServer&) = delete;

    [[nodiscard]] bool start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message = nullptr) override;
    void stop() override;

    [[nodiscard]] bool is_running() const noexcept override;
    [[nodiscard]] bool send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message = nullptr) override;
    void disconnect(ConnectionId connection_id) noexcept override;

private:
    struct ConnectionState;

    void accept_loop();
    void worker_loop();
    void idle_monitor_loop();
    void close_connection(ConnectionId connection_id, bool notify) noexcept;
    void close_all_connections(bool notify) noexcept;
    void report_error(std::string_view message);

    core::Logger& logger_;
    port::SocketSystem socket_system_;
    TcpServerOptions options_{};
    TcpServerCallbacks callbacks_{};
    port::NativeSocket listen_socket_{port::InvalidSocket};
    std::atomic_bool running_{false};
    std::atomic<ConnectionId> next_connection_id_{1};
    void* completion_port_{nullptr};
    std::thread accept_thread_;
    std::thread idle_monitor_thread_;
    std::vector<std::thread> worker_threads_;
    mutable std::mutex connections_mutex_;
    std::unordered_map<ConnectionId, std::shared_ptr<ConnectionState>> connections_;
};

} // namespace serverengine::net
