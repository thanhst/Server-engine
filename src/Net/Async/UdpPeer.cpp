#include "UdpPeer.h"

#include "UdpListener.h"
#include "WorkerContext.h"

#include <utility>

namespace serverengine::net::async {

namespace {
constexpr std::size_t MaxQueuedDatagrams = 1024;

std::size_t queue_cost(std::size_t payload_bytes) noexcept
{
    return payload_bytes == 0 ? 1 : payload_bytes;
}
} // namespace

UdpPeer::UdpPeer(WorkerContext& context, std::weak_ptr<UdpListener> listener,
    Peer peer, boost::asio::ip::udp::endpoint endpoint)
    : context_(context), listener_(std::move(listener)), peer_(std::move(peer)),
      endpoint_(std::move(endpoint))
{
}

const Peer& UdpPeer::peer() const noexcept { return peer_; }

const boost::asio::ip::udp::endpoint& UdpPeer::endpoint() const noexcept { return endpoint_; }

void UdpPeer::start()
{
    if (closed_ || was_open_) return;
    if (!context_.notify_open(peer_)) {
        close();
        return;
    }
    was_open_ = true;
}

bool UdpPeer::send(const core::Buffer& message, std::string* error)
{
    if (closed_ || !was_open_ || context_.stopping) {
        if (error) *error = "UDP peer is closed";
        return false;
    }
    if (message.size() > context_.limits.max_message_bytes || message.size() > MaxUdpPayloadBytes) {
        if (error) *error = "UDP datagram exceeds the configured limit or 65507-byte payload ceiling";
        return false;
    }
    const auto cost = queue_cost(message.size());
    if (send_queue_.size() >= MaxQueuedDatagrams ||
        cost > context_.limits.max_send_queue_bytes ||
        queued_bytes_ > context_.limits.max_send_queue_bytes - cost) {
        if (error) *error = "UDP peer send queue is full";
        return false;
    }

    const bool writing = !send_queue_.empty();
    send_queue_.push_back(std::make_shared<core::Buffer>(message));
    queued_bytes_ += cost;
    if (!writing) send_next();
    return true;
}

void UdpPeer::close()
{
    if (closed_) return;
    closed_ = true;
    send_queue_.clear();
    queued_bytes_ = 0;
    if (const auto listener = listener_.lock()) listener->forget_peer(endpoint_);
    context_.remove_connection(peer_, was_open_);
}

void UdpPeer::receive(const core::Buffer::Byte* data, std::size_t bytes)
{
    if (closed_ || !was_open_ || context_.stopping) return;
    // Outbound traffic does not prove that a UDP peer is still alive.
    last_received_ = std::chrono::steady_clock::now();
    if (!context_.notify_message(peer_, core::Buffer(data, bytes))) close();
}

void UdpPeer::send_next()
{
    if (closed_ || send_queue_.empty()) return;
    if (const auto listener = listener_.lock()) {
        listener->send_datagram(shared_from_this(), send_queue_.front());
    } else {
        close();
    }
}

void UdpPeer::complete_send(const boost::system::error_code& error, std::size_t bytes)
{
    if (closed_) return;
    if (error || bytes != send_queue_.front()->size()) {
        context_.report_error(peer_.listener_id, error ?
            "UDP send failed: " + error.message() : "UDP send returned an incomplete datagram");
        close();
        return;
    }
    queued_bytes_ -= queue_cost(send_queue_.front()->size());
    send_queue_.pop_front();
    send_next();
}

bool UdpPeer::idle_for(std::uint64_t milliseconds) const noexcept
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - last_received_).count();
    return elapsed >= 0 && static_cast<std::uint64_t>(elapsed) >= milliseconds;
}

} // namespace serverengine::net::async
