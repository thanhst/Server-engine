#pragma once

#include <ServerEngine/Net/Endpoint.h>
#include <ServerEngine/Net/TcpBackend.h>
#include <ServerEngine/Net/TransportKind.h>
#include <ServerEngine/Security/SecurityMode.h>

#include <cstddef>
#include <string>
#include <vector>

namespace serverengine::runtime {

struct ListenerOptions {
    std::string name{"main"};
    net::TransportKind transport{net::TransportKind::Tcp};
    net::Endpoint bind_endpoint{"0.0.0.0", 8080};
    bool enabled{true};
};

struct ServerOptions {
    std::size_t worker_count{4};
    std::size_t max_sessions{10000};
    std::size_t max_message_bytes{65536};
    std::uint64_t idle_timeout_ms{300000};
    int receive_timeout_ms{1000};
    bool tcp_no_delay{true};
    net::TcpBackend tcp_backend{net::default_tcp_backend()};
    security::SecurityMode security_mode{security::SecurityMode::Token};
    std::vector<ListenerOptions> listeners{};
};

struct StorageOptions {
    bool enabled{false};
    std::string provider{};
    std::string connection_string{};
    std::size_t pool_size{16};
};

struct DistributionOptions {
    bool enabled{false};
    std::string node_id{"node-1"};
    std::string advertised_endpoint{};
    std::vector<std::string> seed_nodes{};
};

} // namespace serverengine::runtime
