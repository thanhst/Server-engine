#pragma once

#include <optional>
#include <string_view>

namespace serverengine::net {

enum class TransportKind {
    Tcp,
    Udp,
    WebSocket
};

[[nodiscard]] std::string_view to_string(TransportKind kind) noexcept;
[[nodiscard]] std::optional<TransportKind> parse_transport_kind(std::string_view value);

} // namespace serverengine::net
