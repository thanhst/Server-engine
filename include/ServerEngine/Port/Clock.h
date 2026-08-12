#pragma once

#include <chrono>
#include <cstdint>

namespace serverengine::port {

using Milliseconds = std::chrono::milliseconds;

struct DateTime {
    int year{};
    int month{};
    int day{};
    int hour{};
    int minute{};
    int second{};
    int millisecond{};
};

[[nodiscard]] std::uint64_t steady_milliseconds();

[[nodiscard]] std::uint64_t system_milliseconds();

[[nodiscard]] DateTime local_date_time();

void sleep_for(Milliseconds duration);

} // namespace serverengine::port
