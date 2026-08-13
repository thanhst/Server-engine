#pragma once

#include <ServerEngine/Core/Buffer.h>
#include <ServerEngine/Net/Endpoint.h>

#include <cstdint>
#include <functional>
#include <string_view>

namespace serverengine::net {

using ConnectionId = std::uint64_t;

struct TcpServerCallbacks {
    std::function<bool(ConnectionId connection_id, const Endpoint& remote_endpoint)> on_accepting;
    std::function<void(ConnectionId connection_id, const Endpoint& remote_endpoint)> on_connected;
    std::function<void(ConnectionId connection_id, const Endpoint& remote_endpoint, const core::Buffer& message)> on_message;
    std::function<void(ConnectionId connection_id, const Endpoint& remote_endpoint)> on_disconnected;
    std::function<void(std::string_view message)> on_error;
};

struct TcpServerOptions {
    Endpoint bind_endpoint{};
    std::size_t worker_count{4};
    std::size_t max_connections{10000};
    std::size_t max_message_bytes{65536};
    std::uint64_t idle_timeout_ms{300000};
    int receive_timeout_ms{1000};
    bool tcp_no_delay{true};
};

} // namespace serverengine::net
