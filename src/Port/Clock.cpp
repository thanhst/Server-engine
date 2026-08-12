#include <ServerEngine/Port/Clock.h>

#include <ServerEngine/Port/Platform.h>

#include <ctime>
#include <thread>

namespace serverengine::port {

std::uint64_t steady_milliseconds()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::uint64_t system_milliseconds()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

DateTime local_date_time()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds_since_epoch =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const auto milliseconds = static_cast<int>(milliseconds_since_epoch % 1000);
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};

#if SERVERENGINE_OS_WINDOWS
    localtime_s(&local_time, &time);
#elif SERVERENGINE_OS_LINUX || SERVERENGINE_OS_MACOS
    localtime_r(&time, &local_time);
#else
    if (const auto* value = std::localtime(&time)) {
        local_time = *value;
    }
#endif

    return DateTime{
        local_time.tm_year + 1900,
        local_time.tm_mon + 1,
        local_time.tm_mday,
        local_time.tm_hour,
        local_time.tm_min,
        local_time.tm_sec,
        milliseconds
    };
}

void sleep_for(Milliseconds duration)
{
    if (duration.count() <= 0) {
        return;
    }

    std::this_thread::sleep_for(duration);
}

} // namespace serverengine::port
