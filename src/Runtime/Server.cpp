#include <ServerEngine/Runtime/Server.h>

#include <ServerEngine/Net/Endpoint.h>
#include <ServerEngine/Net/ITcpServer.h>
#include <ServerEngine/Net/TcpBackend.h>
#include <ServerEngine/Net/TcpIocpServer.h>
#include <ServerEngine/Net/TcpServer.h>
#include <ServerEngine/Net/TransportKind.h>

#include <exception>
#include <utility>

namespace serverengine::runtime {

Server::Server(ServerOptions options, IMessageHandler& handler, core::Logger& logger)
    : options_(std::move(options))
    , handler_(handler)
    , logger_(logger)
{
}

Server::~Server()
{
    stop();
}

bool Server::start(std::string* error_message)
{
    if (running_) {
        return true;
    }

    if (options_.listeners.empty()) {
        if (error_message != nullptr) {
            *error_message = "server has no listeners";
        }
        return false;
    }

    for (const auto& listener : options_.listeners) {
        if (!listener.enabled) {
            logger_.debug("Listener disabled: ", listener.name);
            continue;
        }

        if (listener.transport != net::TransportKind::Tcp) {
            if (error_message != nullptr) {
                *error_message = "transport is not implemented yet: " + std::string(net::to_string(listener.transport));
            }
            return false;
        }
    }

    running_ = true;
    logger_.info(
        "Runtime started with workers=",
        options_.worker_count,
        " max_sessions=",
        options_.max_sessions,
        " max_message_bytes=",
        options_.max_message_bytes,
        " idle_timeout_ms=",
        options_.idle_timeout_ms);

    for (const auto& listener : options_.listeners) {
        if (!listener.enabled) {
            continue;
        }

        std::unique_ptr<net::ITcpServer> tcp_server;
        if (options_.tcp_backend == net::TcpBackend::Iocp) {
            tcp_server = std::make_unique<net::TcpIocpServer>(logger_);
        } else {
            tcp_server = std::make_unique<net::TcpServer>(logger_);
        }

        auto* tcp_server_ptr = tcp_server.get();

        net::TcpServerCallbacks callbacks;
        callbacks.on_accepting = [this](net::ConnectionId connection_id, const net::Endpoint& remote_endpoint) {
            if (hub_.count() >= options_.max_sessions) {
                logger_.warning(
                    "Connection rejected max_sessions reached connection=",
                    connection_id,
                    " remote=",
                    net::to_string(remote_endpoint));
                return false;
            }

            return true;
        };

        callbacks.on_connected = [this, transport = listener.transport, tcp_server_ptr](
                                     net::ConnectionId connection_id,
                                     const net::Endpoint& remote_endpoint) {
            hub_.add_session(connection_id, transport, remote_endpoint);
            Session session(
                connection_id,
                transport,
                remote_endpoint,
                [tcp_server_ptr, connection_id](SessionId, const core::Buffer& data, std::string* send_error) {
                    return tcp_server_ptr->send(connection_id, data, send_error);
                },
                &hub_);
            dispatch_session_started(session);
        };

        callbacks.on_message = [this, transport = listener.transport, tcp_server_ptr](
                                   net::ConnectionId connection_id,
                                   const net::Endpoint& remote_endpoint,
                                   const core::Buffer& message) {
            Session session(
                connection_id,
                transport,
                remote_endpoint,
                [tcp_server_ptr, connection_id](SessionId, const core::Buffer& data, std::string* send_error) {
                    return tcp_server_ptr->send(connection_id, data, send_error);
                },
                &hub_);
            dispatch_message(session, message);
        };

        callbacks.on_disconnected = [this, transport = listener.transport, tcp_server_ptr](
                                        net::ConnectionId connection_id,
                                        const net::Endpoint& remote_endpoint) {
            Session session(
                connection_id,
                transport,
                remote_endpoint,
                [tcp_server_ptr, connection_id](SessionId, const core::Buffer& data, std::string* send_error) {
                    return tcp_server_ptr->send(connection_id, data, send_error);
                },
                &hub_);
            dispatch_session_stopped(session);
            hub_.remove_session(connection_id);
        };

        callbacks.on_error = [this](std::string_view message) {
            report_handler_error(message);
        };

        std::string transport_error;
        net::TcpServerOptions tcp_options;
        tcp_options.bind_endpoint = listener.bind_endpoint;
        tcp_options.worker_count = options_.worker_count;
        tcp_options.max_connections = options_.max_sessions;
        tcp_options.max_message_bytes = options_.max_message_bytes;
        tcp_options.idle_timeout_ms = options_.idle_timeout_ms;
        tcp_options.receive_timeout_ms = options_.receive_timeout_ms;
        tcp_options.tcp_no_delay = options_.tcp_no_delay;

        if (!tcp_server->start(tcp_options, std::move(callbacks), &transport_error)) {
            stop();
            if (error_message != nullptr) {
                *error_message = "failed to start listener '" + listener.name + "': " + transport_error;
            }
            return false;
        }

        logger_.info(
            "Listener started name=",
            listener.name,
            " transport=",
            net::to_string(listener.transport),
            " tcp_backend=",
            net::to_string(options_.tcp_backend),
            " endpoint=",
            net::to_string(listener.bind_endpoint));

        tcp_servers_.push_back(std::move(tcp_server));
    }

    return true;
}

void Server::stop()
{
    if (!running_) {
        return;
    }

    running_ = false;
    for (auto& tcp_server : tcp_servers_) {
        tcp_server->stop();
    }
    tcp_servers_.clear();

    logger_.info("Runtime stopped");
}

bool Server::is_running() const noexcept
{
    return running_;
}

const ServerOptions& Server::options() const noexcept
{
    return options_;
}

ConnectionHub& Server::hub() noexcept
{
    return hub_;
}

const ConnectionHub& Server::hub() const noexcept
{
    return hub_;
}

void Server::dispatch_session_started(Session& session)
{
    if (!running_) {
        logger_.warning("Ignoring session start while runtime is stopped: session=", session.id());
        return;
    }

    try {
        logger_.info(
            "Session started id=",
            session.id(),
            " transport=",
            net::to_string(session.transport()),
            " remote=",
            net::to_string(session.remote_endpoint()));
        handler_.on_session_started(session);
    } catch (const std::exception& error) {
        report_handler_error(error.what());
    } catch (...) {
        report_handler_error("unknown error in on_session_started");
    }
}

void Server::dispatch_message(Session& session, const core::Buffer& message)
{
    if (!running_) {
        logger_.warning("Ignoring message while runtime is stopped: session=", session.id());
        return;
    }

    try {
        hub_.record_received(session.id(), message.size());
        logger_.debug("Dispatching message session=", session.id(), " bytes=", message.size());
        handler_.on_message(session, message);
    } catch (const std::exception& error) {
        report_handler_error(error.what());
    } catch (...) {
        report_handler_error("unknown error in on_message");
    }
}

void Server::dispatch_session_stopped(Session& session)
{
    if (!running_) {
        return;
    }

    try {
        handler_.on_session_stopped(session);
        logger_.info("Session stopped id=", session.id());
    } catch (const std::exception& error) {
        report_handler_error(error.what());
    } catch (...) {
        report_handler_error("unknown error in on_session_stopped");
    }
}

void Server::report_handler_error(std::string_view message)
{
    logger_.error("Handler error: ", message);

    try {
        handler_.on_error(message);
    } catch (...) {
        logger_.critical("Handler threw from on_error");
    }
}

} // namespace serverengine::runtime
