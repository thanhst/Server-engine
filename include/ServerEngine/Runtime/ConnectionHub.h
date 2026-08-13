#pragma once

#include <ServerEngine/Net/Endpoint.h>
#include <ServerEngine/Net/TransportKind.h>
#include <ServerEngine/Runtime/Session.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace serverengine::runtime {

struct SessionSnapshot {
    SessionId id{};
    net::TransportKind transport{net::TransportKind::Tcp};
    net::Endpoint remote_endpoint{};
    bool authenticated{false};
    std::string user_name{};
    std::uint64_t connected_at_ms{};
    std::uint64_t last_seen_at_ms{};
    std::uint64_t messages_received{};
    std::uint64_t messages_sent{};
    std::uint64_t bytes_received{};
    std::uint64_t bytes_sent{};
};

class ConnectionHub final {
public:
    void add_session(SessionId id, net::TransportKind transport, net::Endpoint remote_endpoint);
    void remove_session(SessionId id);

    [[nodiscard]] bool authenticate(SessionId id, std::string_view user_name);
    [[nodiscard]] bool is_authenticated(SessionId id) const;
    [[nodiscard]] std::string user_name(SessionId id) const;

    void record_received(SessionId id, std::size_t byte_count);
    void record_sent(SessionId id, std::size_t byte_count);

    [[nodiscard]] std::optional<SessionSnapshot> find(SessionId id) const;
    [[nodiscard]] std::vector<SessionSnapshot> snapshot() const;
    [[nodiscard]] std::size_t count() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<SessionId, SessionSnapshot> sessions_;
};

} // namespace serverengine::runtime
