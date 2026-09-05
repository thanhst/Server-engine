#include "EventQueue.h"

#include <chrono>
#include <cstring>

namespace serverengine::runtime::host {

EventQueue::EventQueue(std::size_t max_count, std::size_t max_bytes)
    : max_count_(max_count), max_bytes_(max_bytes)
{
}

bool EventQueue::push(EventKind kind, const net::Peer& peer, const core::Buffer& payload) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ || overflow_) {
        return false;
    }
    // Losing a CLOSE silently would leave ghost players in the host. On any
    // overflow, discard the incomplete history and expose one terminal event.
    if (entries_.size() >= max_count_ || payload.size() > max_bytes_ - bytes_) {
        overflow_ = true;
    } else {
        try {
            entries_.push_back({{kind, peer, next_sequence_, payload.size()}, payload});
            ++next_sequence_;
            bytes_ += payload.size();
        } catch (...) {
            overflow_ = true;
        }
    }
    if (overflow_) {
        entries_.clear();
        bytes_ = 0;
    }
    available_.notify_all();
    return !overflow_;
}

PollResult EventQueue::poll(Event& event, void* payload, std::size_t capacity, std::uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(mutex_);
    available_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
        return stopped_ || overflow_ || !entries_.empty();
    });
    if (overflow_) {
        if (overflow_reported_) {
            return PollResult::Stopped;
        }
        event = {EventKind::Overflow, {}, next_sequence_++, 0};
        overflow_reported_ = true;
        return PollResult::Ready;
    }
    if (entries_.empty()) {
        return stopped_ ? PollResult::Stopped : PollResult::Timeout;
    }
    const auto& entry = entries_.front();
    event = entry.event;
    if (entry.payload.size() > capacity) {
        return PollResult::BufferTooSmall;
    }
    if (!entry.payload.empty()) {
        std::memcpy(payload, entry.payload.data(), entry.payload.size());
    }
    bytes_ -= entry.payload.size();
    entries_.pop_front();
    return PollResult::Ready;
}

void EventQueue::finish()
{
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    available_.notify_all();
}

bool EventQueue::overflowed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return overflow_;
}

} // namespace serverengine::runtime::host
