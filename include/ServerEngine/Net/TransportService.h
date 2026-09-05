#pragma once

#include <ServerEngine/Core/Buffer.h>
#include <ServerEngine/Net/HttpTypes.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace serverengine::net {

enum class Protocol : std::uint32_t { Tcp = 1, Udp = 2, WebSocket = 3, Http = 4 };
enum class ChannelSecurity : std::uint32_t { None = 0, Tls = 1 };

struct ListenerConfig {
    std::uint64_t id{};
    Protocol protocol{Protocol::Tcp};
    std::string bind_address{"127.0.0.1"};
    std::uint16_t port{};
    ChannelSecurity security{ChannelSecurity::Tls};
    std::string certificate_chain_file;
    std::string private_key_file;
    std::string websocket_path{"/"};
    std::uint32_t handshake_timeout_ms{10000};
};

struct ServiceLimits {
    std::size_t worker_threads{1}; // This version deliberately supports one worker.
    std::size_t max_connections{4096};
    std::size_t max_message_bytes{65536};
    std::size_t max_send_queue_bytes{1048576};
    std::uint64_t idle_timeout_ms{300000};
};

struct Peer {
    std::uint64_t session_id{};
    std::uint64_t listener_id{};
    Protocol protocol{Protocol::Tcp};
    std::string address;
    std::uint16_t port{};
};

// Invoked only on the transport worker. Copy incoming buffers before returning.
// Return false when the application queue is full; the peer is disconnected.
// Callbacks must not reenter TransportService or throw exceptions.
struct TransportCallbacks {
    std::function<bool(const Peer&)> on_open;
    std::function<bool(const Peer&, const core::Buffer&)> on_message;
    std::function<void(const Peer&)> on_close;
    std::function<void(std::uint64_t listener_id, std::string_view)> on_error;
    std::function<bool(const Peer&, const core::Buffer&)> on_http_request;
};

// Owns listeners, sockets, TLS contexts and one I/O worker. No application code
// runs on it: the DLL runtime callbacks only enqueue events for the host to poll.
// Public methods may be called concurrently. stop() joins the worker and closes
// all peers before returning. send() means accepted into a bounded local queue,
// not delivered or acknowledged by the remote application.
class TransportService final {
public:
    TransportService();
    ~TransportService();
    TransportService(const TransportService&) = delete;
    TransportService& operator=(const TransportService&) = delete;

    [[nodiscard]] bool start(const std::vector<ListenerConfig>& listeners,
        const ServiceLimits& limits, TransportCallbacks callbacks, std::string* error);
    void stop();
    [[nodiscard]] bool send(std::uint64_t session_id, const core::Buffer& message,
        std::string* error);
    [[nodiscard]] bool disconnect(std::uint64_t session_id, std::string* error);
    [[nodiscard]] bool respond_http(std::uint64_t session_id, std::uint64_t request_id,
        const HttpResponse& response, std::string* error);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace serverengine::net
