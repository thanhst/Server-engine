#include "Runtime/Host/EventQueue.h"

#include <future>
#include <iostream>
#include <stdexcept>

using namespace serverengine;
using namespace serverengine::runtime::host;

static void require(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}

int main()
{
    try {
        EventQueue queue(3, 8);
        const net::Peer peer{1, 2, net::Protocol::Tcp, "127.0.0.1", 9000};
        require(queue.push(EventKind::Message, peer, core::Buffer::from_text("abc")), "enqueue");
        Event event;
        require(queue.poll(event, nullptr, 0, 0) == PollResult::BufferTooSmall, "size probe must not consume");
        require(event.payload_size == 3 && event.sequence == 1, "metadata on size probe");
        char bytes[8]{};
        require(queue.poll(event, bytes, sizeof(bytes), 0) == PollResult::Ready, "retry same event");
        require(std::string(bytes, 3) == "abc" && event.sequence == 1, "payload and order preserved");
        require(queue.poll(event, bytes, sizeof(bytes), 0) == PollResult::Timeout, "queue drained");

        require(queue.push(EventKind::Message, peer, core::Buffer::from_text("12345678")), "fill byte limit");
        require(!queue.push(EventKind::Message, peer, core::Buffer::from_text("x")), "bounded queue");
        require(queue.poll(event, nullptr, 0, 0) == PollResult::Ready && event.kind == EventKind::Overflow,
            "overflow replaces incomplete history with terminal event");
        require(queue.poll(event, nullptr, 0, 0) == PollResult::Stopped, "overflow reported once");
        require(!queue.push(EventKind::Open, peer, {}), "no admission after overflow");

        EventQueue count_limited(1, 8);
        require(count_limited.push(EventKind::Open, peer, {}), "empty control event counts");
        require(!count_limited.push(EventKind::Close, peer, {}), "count cap includes control events");

        EventQueue stopped(4, 8);
        auto poller = std::async(std::launch::async, [&] {
            Event next;
            return stopped.poll(next, nullptr, 0, 3000);
        });
        stopped.finish();
        require(poller.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready, "stop wakes pollers");
        require(poller.get() == PollResult::Stopped, "stopped result");
        std::cout << "PASS EventQueue\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
