#pragma once

#include "Listener.h"
#include "WorkerContext.h"

#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <array>
#include <map>
#include <memory>

namespace serverengine::net::async {

class UdpPeer;

// One socket receives complete datagrams. An endpoint is only a routing address,
// never an authenticated identity; UDP peers have no connection handshake.
class UdpListener final : public Listener, public std::enable_shared_from_this<UdpListener> {
public:
    UdpListener(WorkerContext& context, ListenerConfig config);
    [[nodiscard]] bool open(std::string* error);
    void start() override;
    void close() override;

    void forget_peer(const boost::asio::ip::udp::endpoint& endpoint);
    void send_datagram(const std::shared_ptr<UdpPeer>& peer,
        const std::shared_ptr<core::Buffer>& message);

private:
    void receive_next();
    void on_receive(const boost::system::error_code& error, std::size_t bytes);
    void dispatch_datagram(std::size_t bytes);
    void schedule_expiry();
    void expire_idle_peers();

    WorkerContext& context_;
    const ListenerConfig config_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::steady_timer expiry_timer_;
    boost::asio::ip::udp::endpoint sender_;
    // Larger than every supported payload: truncated packets cannot become a
    // seemingly valid message even on platforms that silently truncate reads.
    std::array<core::Buffer::Byte, 65536> receive_buffer_{};
    std::map<boost::asio::ip::udp::endpoint, std::shared_ptr<UdpPeer>> peers_;
    bool started_{false};
    bool closed_{false};
};

[[nodiscard]] std::shared_ptr<Listener> make_udp_listener(WorkerContext& context,
    const ListenerConfig& config, std::string* error);

} // namespace serverengine::net::async
