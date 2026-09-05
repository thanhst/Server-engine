#pragma once

#include "NativeConfig.h"
#include "Options.h"
#include <deque>
#include <unordered_map>
#include <vector>

namespace serverengine::net::gns {

// All Endpoint operations and native callbacks run under Runtime's mutex.
// GNS owns its I/O workers. We never call application code from those workers.
void status_changed(SteamNetConnectionStatusChangedCallback_t* change) noexcept;
uint64_t next_peer_id();

struct Peer {
    uint64_t id;
    bool connected{false};
};
struct QueuedEvent {
    se_datagram_event metadata;
    std::vector<unsigned char> bytes;
};

class Endpoint final {
public:
    Endpoint(se_datagram_handle id, const se_datagram_options& options);
    ~Endpoint();
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    se_status listen(const char* address, uint32_t port);
    se_status connect(const char* address, uint32_t port, uint64_t& peer);
    se_status send(uint64_t peer, uint32_t delivery, const void* data, uint32_t size);
    se_status disconnect(uint64_t peer);
    se_status take(se_datagram_event& event, void* payload, uint32_t capacity);
    void receive();
    void on_status(const SteamNetConnectionStatusChangedCallback_t& change) noexcept;
    void shutdown() noexcept;
    bool owns(const SteamNetConnectionStatusChangedCallback_t& change) const;
    bool stopped() const { return stopped_; }

private:
    bool push(se_datagram_event event, const void* data = nullptr, uint32_t size = 0) noexcept;
    void overflow() noexcept;
    void drop(HSteamNetConnection connection, int reason, const char* message) noexcept;
    using Peers = std::unordered_map<HSteamNetConnection, Peer>;
    Peers::iterator find_peer(uint64_t id);
    const se_datagram_handle id_;
    const se_datagram_options options_;
    ISteamNetworkingSockets* api_;
    HSteamListenSocket listener_{k_HSteamListenSocket_Invalid};
    HSteamNetPollGroup group_{k_HSteamNetPollGroup_Invalid};
    Peers peers_;
    std::deque<QueuedEvent> events_;
    uint64_t event_bytes_{};
    bool stopped_{false}, overflow_{false}, overflow_reported_{false};
};

} // namespace serverengine::net::gns
