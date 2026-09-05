#pragma once

#include <ServerEngine/Runtime/ConnectionHub.h>

#include <cstddef>
#include <mutex>
#include <optional>

namespace serverengine::runtime::detail {

// One registry is shared by all listeners owned by a Server.
class SessionRegistry final {
public:
    SessionRegistry(ConnectionHub& hub, std::size_t max_sessions);

    [[nodiscard]] std::optional<SessionId> try_add(net::TransportKind transport, const net::Endpoint& remote_endpoint);
    void remove(SessionId session_id);

private:
    ConnectionHub& hub_;
    const std::size_t max_sessions_;
    std::mutex admission_mutex_;
    SessionId next_session_id_{1};
};

} // namespace serverengine::runtime::detail
