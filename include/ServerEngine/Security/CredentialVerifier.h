#pragma once

#include <string_view>

namespace serverengine::security {

[[nodiscard]] bool constant_time_equals(std::string_view left, std::string_view right) noexcept;

} // namespace serverengine::security
