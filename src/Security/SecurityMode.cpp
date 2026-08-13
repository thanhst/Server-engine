#include <ServerEngine/Security/SecurityMode.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace serverengine::security {

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

std::string_view to_string(SecurityMode mode) noexcept
{
    switch (mode) {
    case SecurityMode::None:
        return "none";
    case SecurityMode::Token:
        return "token";
    case SecurityMode::EccP256:
        return "ecc-p256";
    }

    return "unknown";
}

std::optional<SecurityMode> parse_security_mode(std::string_view value)
{
    const auto lowered = lowercase_copy(value);
    if (lowered == "none" || lowered == "off") {
        return SecurityMode::None;
    }
    if (lowered == "token" || lowered == "shared-token") {
        return SecurityMode::Token;
    }
    if (lowered == "ecc" || lowered == "ecc-p256" || lowered == "p256") {
        return SecurityMode::EccP256;
    }

    return std::nullopt;
}

} // namespace serverengine::security
