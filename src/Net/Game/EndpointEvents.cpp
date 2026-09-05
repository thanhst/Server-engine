#include "Endpoint.h"
#include <algorithm>
#include <cstring>
#include <memory>

namespace serverengine::net::game {

void Endpoint::overflow() noexcept
{
    overflow_ = true;
    shutdown(); // Do not keep a partial OPEN/MESSAGE/CLOSE history.
}

bool Endpoint::push(se_game_event event, const void* data, uint32_t size) noexcept
{
    if (stopped_) return false;
    const auto cost = uint64_t(sizeof(se_game_event)) + size;
    if (events_.size() >= options_.max_event_queue_count || cost > options_.max_event_queue_bytes - event_bytes_) {
        overflow();
        return false;
    }
    try {
        QueuedEvent queued{event, {}};
        queued.metadata.payload_size = size;
        if (size) {
            const auto* bytes = static_cast<const unsigned char*>(data);
            queued.bytes.assign(bytes, bytes + size);
        }
        events_.push_back(std::move(queued));
        event_bytes_ += cost;
        return true;
    } catch (...) { overflow(); return false; }
}

se_status Endpoint::take(se_game_event& event, void* payload, uint32_t capacity)
{
    if (overflow_ && !overflow_reported_) {
        overflow_reported_ = true;
        event = empty_event();
        event.kind = SE_GAME_OVERFLOW;
        return SE_OK;
    }
    if (stopped_) return SE_STOPPED;
    if (events_.empty()) return SE_TIMEOUT;
    const auto& queued = events_.front();
    event = queued.metadata;
    if (queued.bytes.size() > capacity) return SE_BUFFER_TOO_SMALL;
    if (!queued.bytes.empty()) std::memcpy(payload, queued.bytes.data(), queued.bytes.size());
    event_bytes_ -= sizeof(se_game_event) + queued.bytes.size();
    events_.pop_front();
    return SE_OK;
}

void Endpoint::drop(HSteamNetConnection native, int reason, const char* message) noexcept
{
    const auto found = peers_.find(native);
    if (found == peers_.end()) return;
    auto event = empty_event();
    event.kind = SE_GAME_DISCONNECTED;
    event.peer_id = found->second.id;
    event.reason = reason;
    // Diagnostics supplied here are local constants, not peer-controlled payloads.
    const auto length = (std::min)(std::strlen(message), sizeof(event.message) - 1);
    std::memcpy(event.message, message, length);
    peers_.erase(found);
    api_->CloseConnection(native, 0, "Connection closed", false);
    push(event);
}

bool Endpoint::owns(const SteamNetConnectionStatusChangedCallback_t& change) const
{
    return !stopped_ && (peers_.find(change.m_hConn) != peers_.end() ||
        (listener_ != k_HSteamListenSocket_Invalid && change.m_info.m_hListenSocket == listener_));
}

void Endpoint::on_status(const SteamNetConnectionStatusChangedCallback_t& change) noexcept
{
    try {
        auto found = peers_.find(change.m_hConn);
        if (change.m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting && found == peers_.end()) {
            if (stopped_ || peers_.size() >= options_.max_peers ||
                (!(options_.flags & SE_GAME_ALLOW_REMOTE_UNAUTHENTICATED) && !change.m_info.m_addrRemote.IsLocalHost())) {
                api_->CloseConnection(change.m_hConn, 0, "Admission rejected", false);
                return;
            }
            NativeConfig config(options_, id_, status_changed);
            if (!config.matches(k_ESteamNetworkingConfig_Connection, change.m_hConn) ||
                !api_->SetConnectionPollGroup(change.m_hConn, group_)) {
                api_->CloseConnection(change.m_hConn, 0, "Configuration rejected", false);
                return;
            }
            found = peers_.emplace(change.m_hConn, Peer{next_peer_id()}).first;
            if (api_->AcceptConnection(change.m_hConn) != k_EResultOK)
                drop(change.m_hConn, 0, "Accept failed");
            return;
        }
        if (found == peers_.end()) return; // Stale callback after explicit disconnect.
        if (change.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
            if (change.m_info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unencrypted) {
                drop(change.m_hConn, 0, "Encryption required");
                return;
            }
            if (!found->second.connected) {
                found->second.connected = true;
                auto event = empty_event();
                event.kind = SE_GAME_CONNECTED;
                event.peer_id = found->second.id;
                push(event);
            }
        } else if (change.m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                   change.m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
            drop(change.m_hConn, change.m_info.m_eEndReason, "Transport disconnected");
        }
    } catch (...) {
        api_->CloseConnection(change.m_hConn, 0, "Callback failed", false);
        overflow();
    }
}

void Endpoint::receive()
{
    // Bounded work per pump, fair across endpoints. Native receive buffers are
    // separately capped by NativeConfig, including pre-application buffering.
    for (unsigned budget = 0; budget < 64 && !stopped_; ++budget) {
        SteamNetworkingMessage_t* raw = nullptr;
        const auto count = api_->ReceiveMessagesOnPollGroup(group_, &raw, 1);
        if (count == 0) break;
        if (count < 0 || raw == nullptr) { overflow(); break; }
        const auto release = [](SteamNetworkingMessage_t* value) { value->Release(); };
        std::unique_ptr<SteamNetworkingMessage_t, decltype(release)> message(raw, release);
        const auto found = peers_.find(raw->m_conn);
        if (found == peers_.end()) continue;
        if (raw->m_cbSize < 0 || uint32_t(raw->m_cbSize) > options_.max_message_bytes) {
            drop(raw->m_conn, 0, "Receive limit exceeded");
            continue;
        }
        // RunCallbacks is pumped before receive; do not deliver MESSAGE before OPEN.
        if (!found->second.connected) { drop(raw->m_conn, 0, "Message before connected event"); continue; }
        auto event = empty_event();
        event.kind = SE_GAME_MESSAGE;
        event.peer_id = found->second.id;
        event.delivery = raw->m_nFlags & k_nSteamNetworkingSend_Reliable ? SE_GAME_RELIABLE_ORDERED : SE_GAME_UNRELIABLE;
        push(event, raw->m_pData, uint32_t(raw->m_cbSize));
    }
}

} // namespace serverengine::net::game
