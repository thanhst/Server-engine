#include "UdpListener.h"

#include "UdpPeer.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/v6_only.hpp>

#include <chrono>
#include <exception>
#include <utility>

namespace serverengine::net::async {

UdpListener::UdpListener(WorkerContext& context, ListenerConfig config)
    : context_(context), config_(std::move(config)), socket_(context.io),
      expiry_timer_(context.io)
{
}

bool UdpListener::open(std::string* error)
{
    if (config_.security != ChannelSecurity::None) {
        if (error) *error = "UDP requires explicit plaintext security; DTLS is not implemented";
        return false;
    }

    boost::system::error_code code;
    const auto address = boost::asio::ip::make_address(config_.bind_address, code);
    if (!code) socket_.open(address.is_v6() ? boost::asio::ip::udp::v6() :
        boost::asio::ip::udp::v4(), code);
    // Bind IPv4 and IPv6 independently; avoid mapped addresses and ambiguous
    // cross-family port ownership when two listeners share a port number.
    if (!code && address.is_v6()) socket_.set_option(boost::asio::ip::v6_only(true), code);
    if (!code) socket_.bind({address, config_.port}, code);
    if (code) {
        if (error) *error = "UDP bind failed: " + code.message();
        return false;
    }
    return true;
}

void UdpListener::start()
{
    if (started_ || closed_ || context_.stopping) return;
    started_ = true;
    receive_next();
    schedule_expiry();
}

void UdpListener::close()
{
    if (closed_) return;
    closed_ = true;
    boost::system::error_code ignored;
    socket_.close(ignored);
    // Keep releasing peers even if the throwing timer cancellation fails.
    try { (void)expiry_timer_.cancel(); } catch (...) {}
    while (!peers_.empty()) {
        // Keep the peer alive while close() removes its map/registry entries.
        const auto peer = peers_.begin()->second;
        peer->close();
    }
}

void UdpListener::forget_peer(const boost::asio::ip::udp::endpoint& endpoint)
{
    peers_.erase(endpoint);
}

void UdpListener::receive_next()
{
    if (closed_ || context_.stopping) return;
    try {
        socket_.async_receive_from(boost::asio::buffer(receive_buffer_), sender_,
            [self = shared_from_this()](const boost::system::error_code& error, std::size_t bytes) {
                self->on_receive(error, bytes);
            });
    } catch (...) {
        close(); // An initiation failure must not leave a bound, stalled socket.
        throw;
    }
}

void UdpListener::on_receive(const boost::system::error_code& error, std::size_t bytes)
{
    if (closed_ || context_.stopping) return;
    if (error) {
        if (error == boost::asio::error::operation_aborted) return;
        // Discard oversized packets and asynchronous ICMP errors. Neither says
        // that this shared listener socket has become unusable.
        if (error != boost::asio::error::message_size &&
            error != boost::asio::error::connection_reset &&
            error != boost::asio::error::connection_refused) {
            context_.report_error(config_.id, "UDP receive failed: " + error.message());
            close();
            return;
        }
    } else if (bytes <= context_.limits.max_message_bytes && bytes <= MaxUdpPayloadBytes) {
        try { dispatch_datagram(bytes); }
        catch (const std::exception& failure) {
            const auto found = peers_.find(sender_);
            if (found != peers_.end()) {
                const auto peer = found->second;
                peer->close();
            }
            context_.report_error(config_.id, failure.what());
        }
    }
    receive_next();
}

void UdpListener::dispatch_datagram(std::size_t bytes)
{
    auto found = peers_.find(sender_);
    std::shared_ptr<UdpPeer> connection;
    if (found != peers_.end()) {
        connection = found->second;
    } else {
        if (context_.connections.size() >= context_.limits.max_connections) return;
        const auto id = context_.allocate_session_id();
        if (id == 0) return;
        Peer peer{id, config_.id, Protocol::Udp, sender_.address().to_string(), sender_.port()};
        connection = std::make_shared<UdpPeer>(context_, weak_from_this(), std::move(peer), sender_);
        peers_.emplace(sender_, connection);
        if (!context_.add_connection(connection)) {
            peers_.erase(sender_);
            return;
        }
        connection->start();
    }
    connection->receive(receive_buffer_.data(), bytes);
}

void UdpListener::send_datagram(const std::shared_ptr<UdpPeer>& peer,
    const std::shared_ptr<core::Buffer>& message)
{
    if (closed_ || context_.stopping) {
        peer->close();
        return;
    }
    try {
        socket_.async_send_to(boost::asio::buffer(message->data(), message->size()), peer->endpoint(),
            [self = shared_from_this(), peer, message](
                const boost::system::error_code& error, std::size_t bytes) {
                // Captures keep socket owner and payload alive even if the peer
                // was disconnected while this OS operation was outstanding.
                peer->complete_send(error, bytes);
            });
    } catch (...) {
        peer->close(); // Do not leave queued packets with no send in flight.
        throw;
    }
}

void UdpListener::schedule_expiry()
{
    if (closed_ || context_.stopping) return;
    const auto interval = context_.limits.idle_timeout_ms < 1000 ?
        context_.limits.idle_timeout_ms : std::uint64_t{1000};
    try {
        expiry_timer_.expires_after(std::chrono::milliseconds(interval == 0 ? 1 : interval));
        expiry_timer_.async_wait([self = shared_from_this()](const boost::system::error_code& error) {
            if (error || self->closed_ || self->context_.stopping) return;
            self->expire_idle_peers();
            self->schedule_expiry();
        });
    } catch (...) {
        close(); // Idle eviction is part of the peer-capacity contract.
        throw;
    }
}

void UdpListener::expire_idle_peers()
{
    for (auto position = peers_.begin(); position != peers_.end();) {
        const auto peer = position->second;
        ++position; // close() erases the previous entry, not this iterator.
        if (peer->idle_for(context_.limits.idle_timeout_ms)) peer->close();
    }
}

std::shared_ptr<Listener> make_udp_listener(WorkerContext& context,
    const ListenerConfig& config, std::string* error)
{
    auto listener = std::make_shared<UdpListener>(context, config);
    return listener->open(error) ? listener : nullptr;
}

} // namespace serverengine::net::async
