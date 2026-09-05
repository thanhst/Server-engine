#pragma once

#include <ServerEngine/Net/TransportService.h>

#include <condition_variable>
#include <deque>
#include <mutex>

namespace serverengine::runtime::host {

enum class EventKind : std::uint32_t { Open = 1, Message, Close, Error, Overflow, HttpRequest };
struct Event {
    EventKind kind{};
    net::Peer peer;
    std::uint64_t sequence{};
    std::size_t payload_size{};
};
enum class PollResult { Ready, Timeout, BufferTooSmall, Stopped };

// One mutex owns both accounting and FIFO order. No host callback runs on I/O.
class EventQueue final {
public:
    EventQueue(std::size_t max_count, std::size_t max_bytes);
    bool push(EventKind kind, const net::Peer& peer, const core::Buffer& payload) noexcept;
    PollResult poll(Event& event, void* payload, std::size_t capacity, std::uint32_t timeout_ms);
    void finish();
    bool overflowed() const;

private:
    struct Entry { Event event; core::Buffer payload; };
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::deque<Entry> entries_;
    const std::size_t max_count_;
    const std::size_t max_bytes_;
    std::size_t bytes_{0};
    std::uint64_t next_sequence_{1};
    bool stopped_{false};
    bool overflow_{false};
    bool overflow_reported_{false};
};

} // namespace serverengine::runtime::host
