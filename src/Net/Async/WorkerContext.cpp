#include "WorkerContext.h"

namespace serverengine::net::async {

std::uint64_t WorkerContext::allocate_session_id() noexcept
{
    if (next_session_id == 0) return 0;
    return next_session_id++;
}

std::uint64_t WorkerContext::allocate_http_request_id() noexcept
{
    if (next_http_request_id == 0) return 0;
    return next_http_request_id++;
}

bool WorkerContext::add_connection(const std::shared_ptr<Connection>& connection)
{
    if (stopping || !connection || connection->peer().session_id == 0 ||
        connections.size() >= limits.max_connections) return false;
    return connections.emplace(connection->peer().session_id, connection).second;
}

bool WorkerContext::notify_open(const Peer& peer) noexcept
{
    try { return !callbacks.on_open || callbacks.on_open(peer); }
    catch (...) { return false; }
}

bool WorkerContext::notify_message(const Peer& peer, const core::Buffer& message) noexcept
{
    try { return !callbacks.on_message || callbacks.on_message(peer, message); }
    catch (...) { return false; }
}

bool WorkerContext::notify_http_request(const Peer& peer, const core::Buffer& request) noexcept
{
    try { return callbacks.on_http_request && callbacks.on_http_request(peer, request); }
    catch (...) { return false; }
}

void WorkerContext::remove_connection(const Peer& peer, bool was_open) noexcept
{
    // Copy before erase: erase may release the registry's last owning reference.
    const auto retained = connections.find(peer.session_id);
    if (retained == connections.end()) return;
    const auto keep_alive = retained->second;
    connections.erase(retained);
    if (was_open && callbacks.on_close) {
        try { callbacks.on_close(keep_alive->peer()); } catch (...) {}
    }
}

void WorkerContext::report_error(std::uint64_t listener_id, std::string_view message) noexcept
{
    if (callbacks.on_error) {
        try { callbacks.on_error(listener_id, message); } catch (...) {}
    }
}

} // namespace serverengine::net::async
