#include "NativeConfig.h"
#include "Options.h"

#include <steam/isteamnetworkingutils.h>

namespace serverengine::net::game {

NativeConfig::NativeConfig(const se_game_options& options, uint64_t user_data,
    FnSteamNetConnectionStatusChanged callback)
{
    values[0].SetInt32(k_ESteamNetworkingConfig_Unencrypted, 0);
    // Direct-IP mode deliberately has no certificates/verified peer identity.
    // Endpoint address checks restrict this to loopback unless explicitly opted in.
    values[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 2);
    values[2].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, int32(options.connect_timeout_ms));
    values[3].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, int32(options.max_send_queue_bytes));
    values[4].SetInt32(k_ESteamNetworkingConfig_RecvBufferSize, int32(native_receive_bytes(options)));
    values[5].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages, int32(native_receive_count(options)));
    values[6].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize, int32(options.max_message_bytes));
    values[7].SetInt32(k_ESteamNetworkingConfig_RecvMaxSegmentsPerPacket, 256);
    values[8].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, int64(user_data));
    values[9].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
        reinterpret_cast<void*>(callback));
}

bool NativeConfig::matches(ESteamNetworkingConfigScope scope, intptr_t object) const
{
    auto* utils = SteamNetworkingUtils();
    if (utils == nullptr) return false;
    // Verify effective limits: reject a build that silently clamps/ignores them.
    for (size_t index = 0; index != 8; ++index) {
        int32 actual = 0;
        size_t size = sizeof(actual);
        ESteamNetworkingConfigDataType type{};
        const auto result = utils->GetConfigValue(values[index].m_eValue, scope, object,
            &type, &actual, &size);
        if ((result != k_ESteamNetworkingGetConfigValue_OK
                && result != k_ESteamNetworkingGetConfigValue_OKInherited)
            || type != k_ESteamNetworkingConfig_Int32 || size != sizeof(actual)
            || actual != values[index].m_val.m_int32) return false;
    }
    return true;
}

bool parse_address(const char* text, uint32_t port, bool allow_remote,
    SteamNetworkingIPAddr& result)
{
    if (text == nullptr || port == 0 || port > 65535) return false;
    size_t length = 0;
    unsigned dots = 0;
    unsigned colons = 0;
    // A bounded numeric IP literal, without brackets, scope ID, DNS or a port.
    while (length < 64 && text[length] != '\0') {
        const char c = text[length++];
        if (c == '.') ++dots;
        else if (c == ':') ++colons;
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F'))) return false;
    }
    if (length == 0 || length == 64 || colons == 1 || (colons == 0 && dots != 3)) return false;
    result.Clear();
    if (!result.ParseString(text) || result.m_port != 0
        || (!allow_remote && !result.IsLocalHost())) return false;
    result.m_port = uint16(port);
    return true;
}

} // namespace serverengine::net::game
