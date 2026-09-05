#pragma once

#include "Connection.h"

#include <boost/asio/io_context.hpp>
#include <memory>
#include <unordered_map>

namespace serverengine::net::async {

// The worker owns this registry, including handshakes in progress. Global
// admission therefore counts TCP, UDP and WebSocket peers together.
struct WorkerContext {
    boost::asio::io_context io;
    ServiceLimits limits;
    TransportCallbacks callbacks;
    bool stopping{false};
    std::unordered_map<std::uint64_t, std::shared_ptr<Connection>> connections;
    std::uint64_t next_session_id{1};
    std::uint64_t next_http_request_id{1};

    // Returns zero after ID exhaustion; IDs are never reused during a service's
    // lifetime (including stop/start). Caller must reject admission on zero.
    [[nodiscard]] std::uint64_t allocate_session_id() noexcept;
    [[nodiscard]] std::uint64_t allocate_http_request_id() noexcept;
    [[nodiscard]] bool add_connection(const std::shared_ptr<Connection>& connection);
    [[nodiscard]] bool notify_open(const Peer& peer) noexcept;
    [[nodiscard]] bool notify_message(const Peer& peer, const core::Buffer& message) noexcept;
    [[nodiscard]] bool notify_http_request(const Peer& peer, const core::Buffer& request) noexcept;
    void remove_connection(const Peer& peer, bool was_open) noexcept;
    void report_error(std::uint64_t listener_id, std::string_view message) noexcept;
};

} // namespace serverengine::net::async
