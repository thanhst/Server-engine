#pragma once

#include <ServerEngine/Net/TcpServer.h>
#include <ServerEngine/Port/Socket.h>

#include "ClientConnection.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace serverengine::net {

// Coordinates listener lifetime and connection membership. Each connection
// owns its socket and receive behavior; platform calls live in SocketOperations.
class TcpServer::Impl final {
public:
    explicit Impl(core::Logger& logger);
    ~Impl();

    [[nodiscard]] bool start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message);
    void stop();
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] bool send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message);
    void disconnect(ConnectionId connection_id) noexcept;

private:
    struct ClientThread {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> finished;
    };

    void accept_loop();
    void launch_client(std::shared_ptr<threaded::ClientConnection> client);
    void reap_finished_clients();
    void run_client(std::shared_ptr<threaded::ClientConnection> client);
    void report_error(std::string_view message);

    core::Logger& logger_;
    port::SocketSystem socket_system_;
    TcpServerOptions options_{};
    TcpServerCallbacks callbacks_{};
    port::NativeSocket listen_socket_{port::InvalidSocket};
    std::atomic_bool running_{false};
    ConnectionId next_connection_id_{1}; // Written only by the accept thread.
    std::thread accept_thread_;

    // The mutex protects membership, never blocking I/O or user callbacks.
    std::mutex clients_mutex_;
    std::unordered_map<ConnectionId, std::shared_ptr<threaded::ClientConnection>> clients_;
    std::vector<ClientThread> client_threads_; // Accept thread writes; stop joins after it exits.
};

} // namespace serverengine::net
