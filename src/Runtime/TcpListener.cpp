#include "TcpListener.h"

#include "SessionRegistry.h"

#include <ServerEngine/Net/TcpIocpServer.h>
#include <ServerEngine/Net/TcpServer.h>
#include <ServerEngine/Runtime/Server.h>

#include <utility>

namespace serverengine::runtime::detail {

TcpListener::TcpListener(Server& server, SessionRegistry& registry, core::Logger& logger, ListenerOptions options)
    : server_(server)
    , registry_(registry)
    , logger_(logger)
    , options_(std::move(options))
{
    if (server_.options().tcp_backend == net::TcpBackend::Iocp) {
        transport_ = std::make_unique<net::TcpIocpServer>(logger_);
    } else {
        transport_ = std::make_unique<net::TcpServer>(logger_);
    }
}

TcpListener::~TcpListener()
{
    stop();
}

bool TcpListener::start(std::string* error_message)
{
    return transport_->start(transport_options(), callbacks(), error_message);
}

void TcpListener::stop()
{
    transport_->stop();

    // All callbacks have finished. Also release sessions if a transport stopped
    // without delivering their final disconnect callbacks.
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (const auto& [connection_id, session_id] : sessions_) {
        static_cast<void>(connection_id);
        registry_.remove(session_id);
    }
    sessions_.clear();
}

net::TcpServerCallbacks TcpListener::callbacks()
{
    net::TcpServerCallbacks result;
    result.on_accepting = [this](net::ConnectionId, const net::Endpoint&) {
        return server_.is_running();
    };
    result.on_connected = [this](net::ConnectionId connection_id, const net::Endpoint& endpoint) {
        on_connected(connection_id, endpoint);
    };
    result.on_message = [this](net::ConnectionId connection_id, const net::Endpoint& endpoint, const core::Buffer& message) {
        on_message(connection_id, endpoint, message);
    };
    result.on_disconnected = [this](net::ConnectionId connection_id, const net::Endpoint& endpoint) {
        on_disconnected(connection_id, endpoint);
    };
    result.on_error = [this](std::string_view message) {
        server_.report_handler_error(message);
    };
    return result;
}

net::TcpServerOptions TcpListener::transport_options() const
{
    const auto& runtime_options = server_.options();
    net::TcpServerOptions result;
    result.bind_endpoint = options_.bind_endpoint;
    result.worker_count = runtime_options.worker_count;
    result.max_connections = runtime_options.max_sessions;
    result.max_message_bytes = runtime_options.max_message_bytes;
    result.idle_timeout_ms = runtime_options.idle_timeout_ms;
    result.receive_timeout_ms = runtime_options.receive_timeout_ms;
    result.tcp_no_delay = runtime_options.tcp_no_delay;
    return result;
}

std::optional<SessionId> TcpListener::find_session(net::ConnectionId connection_id)
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    const auto iterator = sessions_.find(connection_id);
    if (iterator == sessions_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

Session TcpListener::make_session(net::ConnectionId connection_id, SessionId session_id, const net::Endpoint& remote_endpoint)
{
    // A Session is a callback-scoped view. The listener owns the transport and
    // the Server owns the hub for the entire duration of these callbacks.
    return Session(
        session_id,
        options_.transport,
        remote_endpoint,
        [transport = transport_.get(), connection_id](SessionId, const core::Buffer& data, std::string* error_message) {
            return transport->send(connection_id, data, error_message);
        },
        &server_.hub());
}

void TcpListener::on_connected(net::ConnectionId connection_id, const net::Endpoint& remote_endpoint)
{
    if (!server_.is_running()) {
        transport_->disconnect(connection_id);
        return;
    }

    // Admit only after transport setup succeeds: on_accepting may be followed by
    // a setup failure without an on_disconnected callback to release a slot.
    const auto session_id = registry_.try_add(options_.transport, remote_endpoint);
    if (!session_id) {
        logger_.warning("Connection rejected max_sessions reached listener=", options_.name,
            " connection=", connection_id, " remote=", net::to_string(remote_endpoint));
        transport_->disconnect(connection_id);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.emplace(connection_id, *session_id);
    }

    auto session = make_session(connection_id, *session_id, remote_endpoint);
    server_.dispatch_session_started(session);
}

void TcpListener::on_message(net::ConnectionId connection_id, const net::Endpoint& remote_endpoint, const core::Buffer& message)
{
    const auto session_id = find_session(connection_id);
    if (!session_id) {
        return;
    }

    auto session = make_session(connection_id, *session_id, remote_endpoint);
    server_.dispatch_message(session, message);
}

void TcpListener::on_disconnected(net::ConnectionId connection_id, const net::Endpoint& remote_endpoint)
{
    std::optional<SessionId> session_id;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        const auto iterator = sessions_.find(connection_id);
        if (iterator == sessions_.end()) {
            return;
        }
        session_id = iterator->second;
        sessions_.erase(iterator);
    }

    // Never invoke application callbacks while holding the mapping mutex.
    auto session = make_session(connection_id, *session_id, remote_endpoint);
    server_.dispatch_session_stopped(session);
    registry_.remove(*session_id);
}

} // namespace serverengine::runtime::detail
