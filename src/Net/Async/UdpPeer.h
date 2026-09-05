#pragma once

#include "Connection.h"

#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <deque>
#include <memory>

namespace serverengine::net::async {

class UdpListener;
struct WorkerContext;

// Conservative payload ceiling usable by both IPv4 and IPv6. Applications
// should use smaller packets (for example 1200 bytes) to avoid IP fragmentation.
inline constexpr std::size_t MaxUdpPayloadBytes = 65507;

class UdpPeer final : public Connection, public std::enable_shared_from_this<UdpPeer> {
public:
    UdpPeer(WorkerContext& context, std::weak_ptr<UdpListener> listener,
        Peer peer, boost::asio::ip::udp::endpoint endpoint);

    [[nodiscard]] const Peer& peer() const noexcept override;
    [[nodiscard]] const boost::asio::ip::udp::endpoint& endpoint() const noexcept;
    void start() override;
    [[nodiscard]] bool send(const core::Buffer& message, std::string* error) override;
    void close() override;

    void receive(const core::Buffer::Byte* data, std::size_t bytes);
    void complete_send(const boost::system::error_code& error, std::size_t bytes);
    [[nodiscard]] bool idle_for(std::uint64_t milliseconds) const noexcept;

private:
    void send_next();

    WorkerContext& context_;
    const std::weak_ptr<UdpListener> listener_;
    const Peer peer_;
    const boost::asio::ip::udp::endpoint endpoint_;
    std::chrono::steady_clock::time_point last_received_{std::chrono::steady_clock::now()};
    // A queued empty datagram costs one byte of capacity. A second, 1024-packet
    // ceiling bounds allocation overhead for tiny datagrams as well.
    std::deque<std::shared_ptr<core::Buffer>> send_queue_;
    std::size_t queued_bytes_{0};
    bool was_open_{false};
    bool closed_{false};
};

} // namespace serverengine::net::async
