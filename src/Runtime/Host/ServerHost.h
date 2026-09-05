#pragma once

#include "EventQueue.h"

namespace serverengine::runtime::host {

struct HostOptions {
    net::ServiceLimits network;
    std::size_t max_event_queue_count{8192};
    std::size_t max_event_queue_bytes{16 * 1024 * 1024};
};

// The DLL owns this host. It joins transport work before the event queue dies.
// API calls use lifecycle_mutex_; worker callbacks use only EventQueue's lock.
class ServerHost final {
public:
    explicit ServerHost(HostOptions options);
    ~ServerHost();
    bool add_listener(net::ListenerConfig config, std::uint64_t& id, std::string& error);
    bool start(std::string& error);
    void stop();
    bool send(std::uint64_t id, const core::Buffer& data, std::string& error);
    bool disconnect(std::uint64_t id, std::string& error);
    bool respond_http(std::uint64_t session_id, std::uint64_t request_id,
        const net::HttpResponse& response, std::string& error);
    PollResult poll(Event& event, void* payload, std::size_t capacity, std::uint32_t timeout_ms);
    bool overflowed() const { return events_.overflowed(); }
    std::size_t max_message_bytes() const { return options_.network.max_message_bytes; }

private:
    enum class State { Configuring, Running, Stopped };
    net::TransportCallbacks callbacks();
    const HostOptions options_;
    std::mutex lifecycle_mutex_;
    State state_{State::Configuring};
    std::uint64_t next_listener_id_{1};
    std::vector<net::ListenerConfig> listeners_;
    EventQueue events_;
    net::TransportService transport_;
};

} // namespace serverengine::runtime::host
