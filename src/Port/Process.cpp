#include <ServerEngine/Port/Process.h>

#include <ServerEngine/Port/Platform.h>

#include <system_error>

#if SERVERENGINE_OS_WINDOWS
#include <windows.h>
#elif SERVERENGINE_OS_LINUX
#include <limits.h>
#include <unistd.h>
#elif SERVERENGINE_OS_MACOS
#include <mach-o/dyld.h>
#include <unistd.h>
#endif

namespace serverengine::port {

std::uint32_t current_process_id()
{
#if SERVERENGINE_OS_WINDOWS
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#elif SERVERENGINE_OS_LINUX || SERVERENGINE_OS_MACOS
    return static_cast<std::uint32_t>(::getpid());
#else
    return 0;
#endif
}

std::filesystem::path current_executable_path()
{
#if SERVERENGINE_OS_WINDOWS
    std::wstring buffer(MAX_PATH, L'\0');

    for (;;) {
        const DWORD copied = ::GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));

        if (copied == 0) {
            return {};
        }

        if (copied < buffer.size()) {
            buffer.resize(copied);
            return std::filesystem::path(buffer);
        }

        buffer.resize(buffer.size() * 2);
    }
#elif SERVERENGINE_OS_LINUX
    char buffer[PATH_MAX] = {};
    const ssize_t copied = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (copied <= 0) {
        return {};
    }

    return std::filesystem::path(std::string(buffer, static_cast<std::size_t>(copied)));
#elif SERVERENGINE_OS_MACOS
    std::uint32_t size = 0;
    static_cast<void>(::_NSGetExecutablePath(nullptr, &size));

    std::string buffer(size, '\0');
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }

    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
#else
    return {};
#endif
}

std::filesystem::path current_working_directory()
{
    std::error_code error;
    auto path = std::filesystem::current_path(error);
    if (error) {
        return {};
    }

    return path;
}

ProcessInfo current_process_info()
{
    return ProcessInfo{
        current_process_id(),
        current_executable_path(),
        current_working_directory()
    };
}

std::string path_to_utf8(const std::filesystem::path& path)
{
    return path.u8string();
}

} // namespace serverengine::port
