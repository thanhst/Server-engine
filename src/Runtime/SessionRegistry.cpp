#include "SessionRegistry.h"

namespace serverengine::runtime::detail {

SessionRegistry::SessionRegistry(ConnectionHub& hub, std::size_t max_sessions)
    : hub_(hub)
    , max_sessions_(max_sessions)
{
}

std::optional<SessionId> SessionRegistry::try_add(net::TransportKind transport, const net::Endpoint& remote_endpoint)
{
    // The limit check and insertion must be one operation across all listeners.
    std::lock_guard<std::mutex> lock(admission_mutex_);
    if (hub_.count() >= max_sessions_) {
        return std::nullopt;
    }

    // Transport connection IDs are only unique inside their own TCP listener.
    const auto session_id = next_session_id_++;
    hub_.add_session(session_id, transport, remote_endpoint);
    return session_id;
}

void SessionRegistry::remove(SessionId session_id)
{
    hub_.remove_session(session_id);
}

} // namespace serverengine::runtime::detail
