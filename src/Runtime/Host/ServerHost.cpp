#include "ServerHost.h"

#include <utility>

namespace serverengine::runtime::host {

ServerHost::ServerHost(HostOptions options)
    : options_(std::move(options)), events_(options_.max_event_queue_count, options_.max_event_queue_bytes)
{
}

ServerHost::~ServerHost() { stop(); }

bool ServerHost::add_listener(net::ListenerConfig config, std::uint64_t& id, std::string& error)
{
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (state_ != State::Configuring || listeners_.size() >= 64) {
        error = "listeners can only be added before start (maximum 64)";
        return false;
    }
    config.id = next_listener_id_;
    listeners_.push_back(std::move(config));
    id = next_listener_id_++;
    return true;
}

bool ServerHost::start(std::string& error)
{
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (state_ == State::Running) {
        return true;
    }
    if (state_ != State::Configuring) {
        error = "server is stopped; create a new handle to restart";
        return false;
    }
    if (!transport_.start(listeners_, options_.network, callbacks(), &error)) {
        return false;
    }
    state_ = State::Running;
    return true;
}

void ServerHost::stop()
{
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (state_ != State::Stopped) {
        transport_.stop();
        state_ = State::Stopped;
        events_.finish();
    }
}

bool ServerHost::send(std::uint64_t id, const core::Buffer& data, std::string& error)
{
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (state_ != State::Running || events_.overflowed()) {
        error = "server is not running or its event queue overflowed";
        return false;
    }
    return transport_.send(id, data, &error);
}

bool ServerHost::disconnect(std::uint64_t id, std::string& error)
{
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (state_ != State::Running) {
        error = "server is not running";
        return false;
    }
    return transport_.disconnect(id, &error);
}

bool ServerHost::respond_http(std::uint64_t session_id, std::uint64_t request_id,
    const net::HttpResponse& response, std::string& error)
{
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (state_ != State::Running || events_.overflowed()) {
        error = "server is not running or its event queue overflowed";
        return false;
    }
    return transport_.respond_http(session_id, request_id, response, &error);
}

PollResult ServerHost::poll(Event& event, void* payload, std::size_t capacity, std::uint32_t timeout_ms)
{
    // Do not take lifecycle_mutex_ while waiting: another thread must be able
    // to stop/destroy the handle and wake this poller.
    const auto result = events_.poll(event, payload, capacity, timeout_ms);
    if (result == PollResult::Ready && event.kind == EventKind::Overflow) {
        stop();
    }
    return result;
}

net::TransportCallbacks ServerHost::callbacks()
{
    net::TransportCallbacks result;
    result.on_open = [this](const net::Peer& peer) {
        return events_.push(EventKind::Open, peer, {});
    };
    result.on_message = [this](const net::Peer& peer, const core::Buffer& data) {
        return events_.push(EventKind::Message, peer, data);
    };
    result.on_http_request = [this](const net::Peer& peer, const core::Buffer& data) {
        return events_.push(EventKind::HttpRequest, peer, data);
    };
    result.on_close = [this](const net::Peer& peer) {
        events_.push(EventKind::Close, peer, {});
    };
    result.on_error = [this](std::uint64_t listener_id, std::string_view message) {
        net::Peer peer;
        peer.listener_id = listener_id;
        // Listener configuration is immutable while the transport worker runs.
        for (const auto& listener : listeners_) {
            if (listener.id == listener_id) { peer.protocol = listener.protocol; break; }
        }
        try {
            events_.push(EventKind::Error, peer, core::Buffer::from_text(message));
        } catch (...) {
            // Allocation failure still reaches the host without an exception
            // crossing into the I/O loop; an empty error is intentionally valid.
            events_.push(EventKind::Error, peer, {});
        }
    };
    return result;
}

} // namespace serverengine::runtime::host
