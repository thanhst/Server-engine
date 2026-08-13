#include <ServerEngine/Runtime/ConnectionHub.h>

#include <ServerEngine/Port/Clock.h>

#include <utility>

namespace serverengine::runtime {

void ConnectionHub::add_session(SessionId id, net::TransportKind transport, net::Endpoint remote_endpoint)
{
    const auto now = port::system_milliseconds();

    SessionSnapshot snapshot;
    snapshot.id = id;
    snapshot.transport = transport;
    snapshot.remote_endpoint = std::move(remote_endpoint);
    snapshot.connected_at_ms = now;
    snapshot.last_seen_at_ms = now;

    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[id] = std::move(snapshot);
}

void ConnectionHub::remove_session(SessionId id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(id);
}

bool ConnectionHub::authenticate(SessionId id, std::string_view user_name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = sessions_.find(id);
    if (iterator == sessions_.end()) {
        return false;
    }

    iterator->second.authenticated = true;
    iterator->second.user_name.assign(user_name.begin(), user_name.end());
    iterator->second.last_seen_at_ms = port::system_milliseconds();
    return true;
}

bool ConnectionHub::is_authenticated(SessionId id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = sessions_.find(id);
    return iterator != sessions_.end() && iterator->second.authenticated;
}

std::string ConnectionHub::user_name(SessionId id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = sessions_.find(id);
    if (iterator == sessions_.end()) {
        return {};
    }

    return iterator->second.user_name;
}

void ConnectionHub::record_received(SessionId id, std::size_t byte_count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = sessions_.find(id);
    if (iterator == sessions_.end()) {
        return;
    }

    iterator->second.messages_received += 1;
    iterator->second.bytes_received += byte_count;
    iterator->second.last_seen_at_ms = port::system_milliseconds();
}

void ConnectionHub::record_sent(SessionId id, std::size_t byte_count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = sessions_.find(id);
    if (iterator == sessions_.end()) {
        return;
    }

    iterator->second.messages_sent += 1;
    iterator->second.bytes_sent += byte_count;
    iterator->second.last_seen_at_ms = port::system_milliseconds();
}

std::optional<SessionSnapshot> ConnectionHub::find(SessionId id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = sessions_.find(id);
    if (iterator == sessions_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<SessionSnapshot> ConnectionHub::snapshot() const
{
    std::vector<SessionSnapshot> result;

    std::lock_guard<std::mutex> lock(mutex_);
    result.reserve(sessions_.size());
    for (const auto& [id, session] : sessions_) {
        static_cast<void>(id);
        result.push_back(session);
    }

    return result;
}

std::size_t ConnectionHub::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

} // namespace serverengine::runtime
