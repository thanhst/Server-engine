#pragma once

#include <optional>
#include <string_view>

namespace serverengine::security {

enum class SecurityMode {
    None,
    Token,
    EccP256
};

[[nodiscard]] std::string_view to_string(SecurityMode mode) noexcept;
[[nodiscard]] std::optional<SecurityMode> parse_security_mode(std::string_view value);

} // namespace serverengine::security
