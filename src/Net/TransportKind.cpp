#include <ServerEngine/Net/TransportKind.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace serverengine::net {

namespace {

[[nodiscard]] std::string lowercase_copy(std::string_view value)
{
    std::string result(value.begin(), value.end());
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

} // namespace

std::string_view to_string(TransportKind kind) noexcept
{
    switch (kind) {
    case TransportKind::Tcp:
        return "tcp";
    case TransportKind::Udp:
        return "udp";
    case TransportKind::WebSocket:
        return "websocket";
    }

    return "unknown";
}

std::optional<TransportKind> parse_transport_kind(std::string_view value)
{
    const auto lowered = lowercase_copy(value);
    if (lowered == "tcp") {
        return TransportKind::Tcp;
    }
    if (lowered == "udp") {
        return TransportKind::Udp;
    }
    if (lowered == "websocket" || lowered == "ws") {
        return TransportKind::WebSocket;
    }

    return std::nullopt;
}

} // namespace serverengine::net
