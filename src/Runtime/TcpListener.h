#pragma once

#include <ServerEngine/Net/ITcpServer.h>
#include <ServerEngine/Runtime/ServerOptions.h>
#include <ServerEngine/Runtime/Session.h>

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace serverengine::core {
class Logger;
}

namespace serverengine::runtime {
class Server;

namespace detail {
class SessionRegistry;

// Owns a transport and translates its local connection IDs into runtime sessions.
// stop() joins transport threads before callback state can be destroyed.
class TcpListener final {
public:
    TcpListener(Server& server, SessionRegistry& registry, core::Logger& logger, ListenerOptions options);
    ~TcpListener();

    [[nodiscard]] bool start(std::string* error_message);
    void stop();

private:
    [[nodiscard]] net::TcpServerCallbacks callbacks();
    [[nodiscard]] net::TcpServerOptions transport_options() const;
    [[nodiscard]] std::optional<SessionId> find_session(net::ConnectionId connection_id);
    [[nodiscard]] Session make_session(net::ConnectionId connection_id, SessionId session_id, const net::Endpoint& remote_endpoint);

    void on_connected(net::ConnectionId connection_id, const net::Endpoint& remote_endpoint);
    void on_message(net::ConnectionId connection_id, const net::Endpoint& remote_endpoint, const core::Buffer& message);
    void on_disconnected(net::ConnectionId connection_id, const net::Endpoint& remote_endpoint);

    Server& server_;
    SessionRegistry& registry_;
    core::Logger& logger_;
    ListenerOptions options_;
    std::unique_ptr<net::ITcpServer> transport_;
    std::mutex sessions_mutex_;
    std::unordered_map<net::ConnectionId, SessionId> sessions_;
};

} // namespace detail
} // namespace serverengine::runtime
