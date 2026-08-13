#pragma once

#include <optional>
#include <string_view>

namespace serverengine::net {

enum class TcpBackend {
    Threaded,
    Iocp
};

[[nodiscard]] std::string_view to_string(TcpBackend backend) noexcept;
[[nodiscard]] std::optional<TcpBackend> parse_tcp_backend(std::string_view value);
[[nodiscard]] TcpBackend default_tcp_backend() noexcept;

} // namespace serverengine::net
