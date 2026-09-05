#pragma once

#include <ServerEngine/C/DatagramTransport.h>
#include <steam/steamnetworkingsockets.h>

#include <array>

namespace serverengine::net::gns {

// A single configuration is applied atomically at native listen/connect creation.
// No vendor configuration or callback pointer is exposed through our public ABI.
struct NativeConfig {
    std::array<SteamNetworkingConfigValue_t, 10> values{};

    NativeConfig(const se_datagram_options& options, uint64_t user_data,
        FnSteamNetConnectionStatusChanged callback);
    bool matches(ESteamNetworkingConfigScope scope, intptr_t object) const;
};

bool parse_address(const char* text, uint32_t port, bool allow_remote,
    SteamNetworkingIPAddr& result);

} // namespace serverengine::net::gns
