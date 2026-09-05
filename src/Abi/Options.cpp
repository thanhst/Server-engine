#include "Options.h"

#include <algorithm>
#include <iterator>

namespace serverengine::abi {
namespace {

template<class Options>
bool valid_header(const Options* options)
{
    return options != nullptr && options->struct_size == sizeof(Options)
        && options->abi_version == SE_ABI_VERSION
        && std::all_of(std::begin(options->reserved), std::end(options->reserved), [](auto value) { return value == 0; });
}

std::string text_or(const char* text, const char* fallback = "")
{
    return text == nullptr ? fallback : text;
}

} // namespace

bool decode(const se_server_options* source, runtime::host::HostOptions& result, std::string& error)
{
    if (!valid_header(source)) {
        error = "invalid options size/version/reserved; use se_server_options_init";
        return false;
    }
    if (source->max_connections == 0 || source->max_connections > 1000000
        || source->max_message_bytes == 0 || source->max_message_bytes > 16 * 1024 * 1024
        || source->max_send_queue_bytes < source->max_message_bytes + 16
        || source->max_send_queue_bytes > 256 * 1024 * 1024
        || source->max_event_queue_count == 0 || source->max_event_queue_count > 1000000
        || source->max_event_queue_bytes < source->max_message_bytes
        || source->max_event_queue_bytes > UINT64_C(1073741824)
        || source->idle_timeout_ms < 100 || source->idle_timeout_ms > UINT64_C(86400000)) {
        error = "limits outside supported range (message <=16MiB, idle 100ms..24h, queues bounded)";
        return false;
    }
    result.network.max_connections = source->max_connections;
    result.network.max_message_bytes = source->max_message_bytes;
    result.network.max_send_queue_bytes = source->max_send_queue_bytes;
    result.network.idle_timeout_ms = source->idle_timeout_ms;
    result.max_event_queue_count = source->max_event_queue_count;
    result.max_event_queue_bytes = static_cast<std::size_t>(source->max_event_queue_bytes);
    return true;
}

bool decode(const se_listener_options* source, net::ListenerConfig& result, std::string& error)
{
    if (!valid_header(source)) {
        error = "invalid listener size/version/reserved; use se_listener_options_init";
        return false;
    }
    if (source->protocol < SE_PROTOCOL_TCP || source->protocol > SE_PROTOCOL_HTTP
        || source->security > SE_SECURITY_TLS || source->port == 0 || source->port > 65535
        || source->handshake_timeout_ms < 100 || source->handshake_timeout_ms > 300000) {
        error = "invalid protocol/security/port/handshake timeout";
        return false;
    }
    if (source->protocol == SE_PROTOCOL_UDP && source->security != SE_SECURITY_NONE) {
        error = "TLS cannot secure UDP; DTLS is not implemented. Select explicit plaintext UDP or use TLS/WSS";
        return false;
    }
    result.protocol = static_cast<net::Protocol>(source->protocol);
    result.security = static_cast<net::ChannelSecurity>(source->security);
    result.bind_address = text_or(source->bind_address, "127.0.0.1");
    result.port = static_cast<std::uint16_t>(source->port);
    result.handshake_timeout_ms = source->handshake_timeout_ms;
    result.certificate_chain_file = text_or(source->certificate_chain_file);
    result.private_key_file = text_or(source->private_key_file);
    result.websocket_path = text_or(source->websocket_path, "/");
    if (result.security == net::ChannelSecurity::Tls
        && (result.certificate_chain_file.empty() || result.private_key_file.empty())) {
        error = "TLS listener requires certificate_chain_file and private_key_file";
        return false;
    }
    if (result.bind_address.size() > 64 || result.websocket_path.empty()
        || result.websocket_path.front() != '/' || result.websocket_path.size() > 2048) {
        error = "invalid bind address or WebSocket path";
        return false;
    }
    return true;
}

} // namespace serverengine::abi
