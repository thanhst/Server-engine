#include <ServerEngine/Security/CredentialVerifier.h>

#include <cstddef>
#include <cstdint>

namespace serverengine::security {

bool constant_time_equals(std::string_view left, std::string_view right) noexcept
{
    std::uint8_t difference = static_cast<std::uint8_t>(left.size() ^ right.size());
    const std::size_t max_size = left.size() > right.size() ? left.size() : right.size();

    for (std::size_t index = 0; index < max_size; ++index) {
        const auto left_byte = index < left.size() ? static_cast<std::uint8_t>(left[index]) : std::uint8_t{0};
        const auto right_byte = index < right.size() ? static_cast<std::uint8_t>(right[index]) : std::uint8_t{0};
        difference = static_cast<std::uint8_t>(difference | (left_byte ^ right_byte));
    }

    return difference == 0;
}

} // namespace serverengine::security
