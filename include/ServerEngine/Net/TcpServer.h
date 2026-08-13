#pragma once

#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Net/ITcpServer.h>
#include <ServerEngine/Port/Socket.h>

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace serverengine::net {

class TcpServer final : public ITcpServer {
public:
    explicit TcpServer(core::Logger& logger);
    ~TcpServer() override;

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    [[nodiscard]] bool start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message = nullptr) override;
    void stop() override;

    [[nodiscard]] bool is_running() const noexcept override;
    [[nodiscard]] bool send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message = nullptr) override;
    void disconnect(ConnectionId connection_id) noexcept override;

private:
    void accept_loop();
    void client_loop(ConnectionId connection_id, port::NativeSocket client_socket, Endpoint remote_endpoint);
    void close_client(ConnectionId connection_id) noexcept;
    void report_error(std::string_view message);

    core::Logger& logger_;
    port::SocketSystem socket_system_;
    TcpServerOptions options_{};
    TcpServerCallbacks callbacks_{};
    port::NativeSocket listen_socket_{port::InvalidSocket};
    std::atomic_bool running_{false};
    std::atomic<ConnectionId> next_connection_id_{1};
    std::thread accept_thread_;
    std::mutex clients_mutex_;
    std::unordered_map<ConnectionId, port::NativeSocket> clients_;
    std::vector<std::thread> client_threads_;
};

} // namespace serverengine::net
