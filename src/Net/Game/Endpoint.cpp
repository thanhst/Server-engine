#include "Endpoint.h"
#include <algorithm>
#include <stdexcept>

namespace serverengine::net::game {
namespace {
// Close an unpublished native connection if allocation/configuration fails.
struct PendingConnection {
    ISteamNetworkingSockets* api;
    HSteamNetConnection handle;
    ~PendingConnection() {
        if (handle != k_HSteamNetConnection_Invalid)
            api->CloseConnection(handle, 0, "Connection setup failed", false);
    }
};
}

Endpoint::Endpoint(se_game_handle id, const se_game_options& options)
    : id_(id), options_(options), api_(SteamNetworkingSockets())
{
    group_ = api_->CreatePollGroup();
    if (group_ == k_HSteamNetPollGroup_Invalid) throw std::runtime_error("GNS poll group creation failed");
}
Endpoint::~Endpoint() { shutdown(); }

Endpoint::Peers::iterator Endpoint::find_peer(uint64_t id)
{
    return std::find_if(peers_.begin(), peers_.end(), [id](const auto& entry) { return entry.second.id == id; });
}

se_status Endpoint::listen(const char* address, uint32_t port)
{
    if (stopped_) return SE_STOPPED;
    SteamNetworkingIPAddr native;
    if (!parse_address(address, port, options_.flags & SE_GAME_ALLOW_REMOTE_UNAUTHENTICATED, native))
        return SE_INVALID_ARGUMENT;
    if (listener_ != k_HSteamListenSocket_Invalid) return SE_INVALID_STATE;
    NativeConfig config(options_, id_, status_changed);
    const auto listener = api_->CreateListenSocketIP(native, int(config.values.size()), config.values.data());
    if (listener == k_HSteamListenSocket_Invalid) return SE_IO_ERROR;
    if (!config.matches(k_ESteamNetworkingConfig_ListenSocket, listener)) {
        api_->CloseListenSocket(listener);
        return SE_IO_ERROR;
    }
    listener_ = listener;
    return SE_OK;
}

se_status Endpoint::connect(const char* address, uint32_t port, uint64_t& peer)
{
    if (stopped_) return SE_STOPPED;
    SteamNetworkingIPAddr native;
    if (!parse_address(address, port, options_.flags & SE_GAME_ALLOW_REMOTE_UNAUTHENTICATED, native))
        return SE_INVALID_ARGUMENT;
    if (peers_.size() >= options_.max_peers) return SE_BACKPRESSURE;
    NativeConfig config(options_, id_, status_changed);
    PendingConnection pending{api_, api_->ConnectByIPAddress(native, int(config.values.size()), config.values.data())};
    if (pending.handle == k_HSteamNetConnection_Invalid) return SE_IO_ERROR;
    if (!config.matches(k_ESteamNetworkingConfig_Connection, pending.handle) ||
        !api_->SetConnectionPollGroup(pending.handle, group_)) return SE_IO_ERROR;
    const auto id = next_peer_id();
    const auto inserted = peers_.emplace(pending.handle, Peer{id});
    if (!inserted.second) return SE_INTERNAL_ERROR;
    pending.handle = k_HSteamNetConnection_Invalid;
    peer = id;
    return SE_OK;
}

se_status Endpoint::send(uint64_t peer, uint32_t delivery, const void* data, uint32_t size)
{
    if (stopped_) return SE_STOPPED;
    if (size > options_.max_message_bytes) return SE_RESULT_TOO_LARGE;
    const auto found = find_peer(peer);
    if (found == peers_.end()) return SE_INVALID_HANDLE;
    if (!found->second.connected) return SE_INVALID_STATE;
    const int flags = delivery == SE_GAME_RELIABLE_ORDERED ?
        k_nSteamNetworkingSend_ReliableNoNagle : k_nSteamNetworkingSend_UnreliableNoNagle;
    const unsigned char empty = 0;
    const auto result = api_->SendMessageToConnection(found->first, size ? data : &empty, size, flags, nullptr);
    switch (result) {
    case k_EResultOK: return SE_OK;
    case k_EResultLimitExceeded: return SE_BACKPRESSURE;
    case k_EResultInvalidParam: return SE_INVALID_ARGUMENT;
    case k_EResultInvalidState: return SE_INVALID_STATE;
    case k_EResultNoConnection: return SE_INVALID_STATE;
    default: return SE_IO_ERROR;
    }
}

se_status Endpoint::disconnect(uint64_t id)
{
    if (stopped_) return SE_STOPPED;
    const auto found = find_peer(id);
    if (found == peers_.end()) return SE_INVALID_HANDLE;
    drop(found->first, 0, "Local disconnect");
    return SE_OK;
}

void Endpoint::shutdown() noexcept
{
    stopped_ = true;
    // No linger: API makes no promise of delivery after endpoint destruction.
    for (const auto& peer : peers_) api_->CloseConnection(peer.first, 0, "Endpoint stopped", false);
    peers_.clear();
    if (listener_ != k_HSteamListenSocket_Invalid) {
        api_->CloseListenSocket(listener_);
        listener_ = k_HSteamListenSocket_Invalid;
    }
    if (group_ != k_HSteamNetPollGroup_Invalid) {
        api_->DestroyPollGroup(group_);
        group_ = k_HSteamNetPollGroup_Invalid;
    }
    events_.clear();
    event_bytes_ = 0;
}

} // namespace serverengine::net::game
