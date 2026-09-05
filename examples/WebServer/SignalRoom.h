#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string_view>

// Lab-only room: two participants, no accounts or private-room authentication.
// SDP/ICE is opaque to this class. Audio/video never passes through this relay.
class SignalRoom final {
public:
    using Send = std::function<bool(std::uint64_t, std::string_view)>;
    using Disconnect = std::function<void(std::uint64_t)>;
    SignalRoom(Send send, Disconnect disconnect);
    void opened(std::uint64_t session);
    void message(std::uint64_t session, std::string_view payload);
    void closed(std::uint64_t session);
private:
    std::array<std::uint64_t, 2> members_{};
    Send send_;
    Disconnect disconnect_;
};
