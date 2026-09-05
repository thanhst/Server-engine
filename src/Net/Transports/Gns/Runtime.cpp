#include "Api.h"
#include "Endpoint.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace serverengine::net::gns {
namespace {
struct Runtime {
    std::mutex mutex;
    std::condition_variable changed;
    std::unordered_map<se_datagram_handle, std::shared_ptr<Endpoint>> endpoints;
    uint64_t next_endpoint{1}, next_peer{1};
    bool initialized{false};

    ~Runtime() {
        for (auto& entry : endpoints) entry.second->shutdown();
        endpoints.clear();
        if (initialized) GameNetworkingSockets_Kill();
    }
    void init() {
        if (initialized) return;
        SteamNetworkingErrMsg error{};
        if (!GameNetworkingSockets_Init(nullptr, error))
            throw std::runtime_error(std::string("GNS initialization failed: ") + error);
        initialized = true;
    }
    void release_if_empty() {
        if (initialized && endpoints.empty()) {
            GameNetworkingSockets_Kill();
            initialized = false;
        }
    }
    std::shared_ptr<Endpoint> find(se_datagram_handle id) {
        const auto found = endpoints.find(id);
        return found == endpoints.end() ? nullptr : found->second;
    }
    void pump() {
        if (!initialized) return;
        SteamNetworkingSockets()->RunCallbacks();
        for (const auto& entry : endpoints) entry.second->receive();
    }
};
Runtime& runtime() { static Runtime instance; return instance; }

template<class Operation>
se_status with_endpoint(se_datagram_handle id, Operation operation) {
    auto& state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto endpoint = state.find(id);
    return endpoint ? operation(*endpoint) : SE_INVALID_HANDLE;
}
}

uint64_t next_peer_id()
{
    auto& state = runtime(); // Caller already holds mutex; never reuse public IDs.
    if (state.next_peer > UINT64_C(0x00ffffffffffffff)) throw std::overflow_error("Datagram peer ID space exhausted");
    return UINT64_C(0x5000000000000000) | state.next_peer++;
}

void status_changed(SteamNetConnectionStatusChangedCallback_t* change) noexcept
{
    // GNS invokes this only from RunCallbacks, which our pump holds mutex around.
    auto& state = runtime();
    for (const auto& entry : state.endpoints) {
        if (entry.second->owns(*change)) {
            entry.second->on_status(*change);
            return;
        }
    }
    // An unaccepted inbound connection can outlive listener/endpoint destruction.
    if (change->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting)
        SteamNetworkingSockets()->CloseConnection(change->m_hConn, 0, "Endpoint unavailable", false);
}

se_status create(const se_datagram_options& options, se_datagram_handle& output)
{
    auto& state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    // Also cap endpoint count: each endpoint has separate native queue budgets.
    if (state.endpoints.size() >= 64) return SE_BACKPRESSURE;
    if (state.next_endpoint > UINT64_C(0x00ffffffffffffff)) return SE_INTERNAL_ERROR;
    state.init();
    try {
        const auto id = UINT64_C(0x4700000000000000) | state.next_endpoint++;
        auto endpoint = std::make_shared<Endpoint>(id, options);
        state.endpoints.emplace(id, std::move(endpoint));
        output = id;
    } catch (...) { state.release_if_empty(); throw; }
    return SE_OK;
}

se_status listen(se_datagram_handle id, const char* address, uint32_t port)
{
    return with_endpoint(id, [&](Endpoint& endpoint) { return endpoint.listen(address, port); });
}
se_status connect(se_datagram_handle id, const char* address, uint32_t port, uint64_t& peer)
{
    return with_endpoint(id, [&](Endpoint& endpoint) { return endpoint.connect(address, port, peer); });
}
se_status send(se_datagram_handle id, uint64_t peer, uint32_t delivery, const void* data, uint32_t size)
{
    return with_endpoint(id, [&](Endpoint& endpoint) { return endpoint.send(peer, delivery, data, size); });
}
se_status disconnect(se_datagram_handle id, uint64_t peer)
{
    return with_endpoint(id, [&](Endpoint& endpoint) { return endpoint.disconnect(peer); });
}

se_status poll(se_datagram_handle id, se_datagram_event& event, void* payload,
    uint32_t capacity, uint32_t timeout_ms)
{
    auto& state = runtime();
    std::unique_lock<std::mutex> lock(state.mutex);
    // Retain object while waiting. Destroy shuts it down before removing it,
    // allowing this poll to return STOPPED without touching a killed GNS runtime.
    const auto endpoint = state.find(id);
    if (!endpoint) return SE_INVALID_HANDLE;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        // Consume queued events first. A BUFFER_TOO_SMALL retry cannot overflow
        // its retained event just because another receive burst arrived.
        auto result = endpoint->take(event, payload, capacity);
        if (result != SE_TIMEOUT) return result;
        state.pump();
        result = endpoint->take(event, payload, capacity);
        if (result != SE_TIMEOUT || timeout_ms == 0) return result;
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return SE_TIMEOUT;
        state.changed.wait_until(lock, (std::min)(deadline, now + std::chrono::milliseconds(5)));
    }
}

se_status destroy(se_datagram_handle id)
{
    auto& state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto endpoint = state.find(id);
    if (!endpoint) return SE_INVALID_HANDLE;
    endpoint->shutdown();
    state.endpoints.erase(id);
    state.changed.notify_all();
    state.release_if_empty();
    return SE_OK;
}

} // namespace serverengine::net::gns
