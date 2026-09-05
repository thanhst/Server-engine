#include "Server.h"

#if SERVERENGINE_OS_WINDOWS
#include <ServerEngine/Port/Clock.h>

#include <chrono>

namespace serverengine::net::iocp {

Server::CallbackScope::CallbackScope(Server& server, const std::shared_ptr<Connection>& connection)
    : server_(server), connection_(connection), entered_(connection->begin_callback())
{
}

Server::CallbackScope::~CallbackScope()
{
    if (entered_ && connection_->end_callback()) {
        server_.notify_disconnected(connection_);
    }
}

bool Server::send(ConnectionId id, const core::Buffer& data, std::string* error_message)
{
    if (data.empty()) {
        return true;
    }

    std::shared_ptr<Connection> connection;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        const auto iterator = connections_.find(id);
        if (iterator == connections_.end()) {
            set_error(error_message, "connection not found");
            return false;
        }
        connection = iterator->second;
    }
    if (!connection->send(completion_port_, data, error_message)) {
        disconnect(id);
        return false;
    }
    return true;
}

void Server::disconnect(ConnectionId id) noexcept
{
    std::shared_ptr<Connection> connection;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        const auto iterator = connections_.find(id);
        if (iterator == connections_.end()) {
            return;
        }
        connection = iterator->second;
        // Close before removing from the registry: once stop sees an empty
        // registry, no removed connection can still submit a fresh operation.
        connection->close();
        connections_.erase(iterator);
    }
    if (connection->request_disconnect_notification()) {
        notify_disconnected(connection);
    }
}

void Server::notify_disconnected(const std::shared_ptr<Connection>& connection)
{
    if (callbacks_.on_disconnected) {
        callbacks_.on_disconnected(connection->id(), connection->endpoint());
    }
}

std::vector<std::shared_ptr<Connection>> Server::connection_snapshot()
{
    std::lock_guard<std::mutex> lock(connections_mutex_);
    std::vector<std::shared_ptr<Connection>> snapshot;
    snapshot.reserve(connections_.size());
    for (const auto& [id, connection] : connections_) {
        static_cast<void>(id);
        snapshot.push_back(connection);
    }
    return snapshot;
}

void Server::close_all_connections() noexcept
{
    for (const auto& connection : connection_snapshot()) {
        disconnect(connection->id());
    }
}

void Server::idle_monitor_loop()
{
    while (running_) {
        port::sleep_for(std::chrono::milliseconds(1000));
        if (!running_ || options_.idle_timeout_ms == 0) {
            continue;
        }
        const auto now_ms = port::steady_milliseconds();
        for (const auto& connection : connection_snapshot()) {
            if (connection->is_closing()) {
                continue;
            }
            const auto last_activity_ms = connection->last_activity_ms();
            // A worker may record newer activity after this scan began.
            if (last_activity_ms > 0 && now_ms >= last_activity_ms
                && now_ms - last_activity_ms >= options_.idle_timeout_ms) {
                logger_.info("TCP IOCP idle timeout connection=", connection->id(), " remote=", to_string(connection->endpoint()));
                disconnect(connection->id());
            }
        }
    }
}

void Server::report_error(std::string_view message)
{
    logger_.error("TCP IOCP error: ", message);
    if (callbacks_.on_error) {
        callbacks_.on_error(message);
    }
}

} // namespace serverengine::net::iocp
#endif
