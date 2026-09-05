#include <ServerEngine/Runtime/Server.h>

#include "SessionRegistry.h"
#include "TcpListener.h"

#include <ServerEngine/Net/Endpoint.h>
#include <ServerEngine/Net/TcpBackend.h>
#include <ServerEngine/Net/TransportKind.h>

#include <exception>
#include <utility>

namespace serverengine::runtime {

Server::Server(ServerOptions options, IMessageHandler& handler, core::Logger& logger)
    : options_(std::move(options))
    , handler_(handler)
    , logger_(logger)
    , session_registry_(std::make_unique<detail::SessionRegistry>(hub_, options_.max_sessions))
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

        auto tcp_listener = std::make_unique<detail::TcpListener>(*this, *session_registry_, logger_, listener);
        std::string transport_error;
        if (!tcp_listener->start(&transport_error)) {
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

        tcp_listeners_.push_back(std::move(tcp_listener));
    }

    return true;
}

void Server::stop()
{
    if (!running_) {
        return;
    }

    running_ = false;
    for (auto& tcp_listener : tcp_listeners_) {
        tcp_listener->stop();
    }
    tcp_listeners_.clear();

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
