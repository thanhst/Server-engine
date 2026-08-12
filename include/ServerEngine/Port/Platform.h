#pragma once

#include <string_view>

#define SERVERENGINE_DETAIL_STRINGIFY_IMPL(value) #value
#define SERVERENGINE_DETAIL_STRINGIFY(value) SERVERENGINE_DETAIL_STRINGIFY_IMPL(value)

#if defined(_WIN32)
#define SERVERENGINE_OS_WINDOWS 1
#else
#define SERVERENGINE_OS_WINDOWS 0
#endif

#if defined(__linux__)
#define SERVERENGINE_OS_LINUX 1
#else
#define SERVERENGINE_OS_LINUX 0
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define SERVERENGINE_OS_MACOS 1
#else
#define SERVERENGINE_OS_MACOS 0
#endif

#if defined(_MSC_VER)
#define SERVERENGINE_COMPILER_MSVC 1
#else
#define SERVERENGINE_COMPILER_MSVC 0
#endif

#if defined(__clang__)
#define SERVERENGINE_COMPILER_CLANG 1
#else
#define SERVERENGINE_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define SERVERENGINE_COMPILER_GCC 1
#else
#define SERVERENGINE_COMPILER_GCC 0
#endif

namespace serverengine::port {

enum class OperatingSystem {
    Windows,
    Linux,
    MacOS,
    Unknown
};

enum class CpuArchitecture {
    X86,
    X64,
    Arm64,
    Unknown
};

[[nodiscard]] constexpr OperatingSystem current_operating_system() noexcept
{
#if SERVERENGINE_OS_WINDOWS
    return OperatingSystem::Windows;
#elif SERVERENGINE_OS_LINUX
    return OperatingSystem::Linux;
#elif SERVERENGINE_OS_MACOS
    return OperatingSystem::MacOS;
#else
    return OperatingSystem::Unknown;
#endif
}

[[nodiscard]] constexpr CpuArchitecture current_cpu_architecture() noexcept
{
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
    return CpuArchitecture::X64;
#elif defined(_M_IX86) || defined(__i386__)
    return CpuArchitecture::X86;
#elif defined(_M_ARM64) || defined(__aarch64__)
    return CpuArchitecture::Arm64;
#else
    return CpuArchitecture::Unknown;
#endif
}

[[nodiscard]] constexpr bool is_debug_build() noexcept
{
#if defined(NDEBUG)
    return false;
#else
    return true;
#endif
}

[[nodiscard]] constexpr std::string_view to_string(OperatingSystem value) noexcept
{
    switch (value) {
    case OperatingSystem::Windows:
        return "Windows";
    case OperatingSystem::Linux:
        return "Linux";
    case OperatingSystem::MacOS:
        return "macOS";
    case OperatingSystem::Unknown:
        return "Unknown";
    }

    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(CpuArchitecture value) noexcept
{
    switch (value) {
    case CpuArchitecture::X86:
        return "x86";
    case CpuArchitecture::X64:
        return "x64";
    case CpuArchitecture::Arm64:
        return "arm64";
    case CpuArchitecture::Unknown:
        return "unknown";
    }

    return "unknown";
}

[[nodiscard]] constexpr std::string_view compiler_name() noexcept
{
#if SERVERENGINE_COMPILER_MSVC
    return "MSVC";
#elif SERVERENGINE_COMPILER_CLANG
    return "Clang";
#elif SERVERENGINE_COMPILER_GCC
    return "GCC";
#else
    return "Unknown";
#endif
}

[[nodiscard]] constexpr std::string_view compiler_version() noexcept
{
#if SERVERENGINE_COMPILER_MSVC
    return SERVERENGINE_DETAIL_STRINGIFY(_MSC_FULL_VER);
#elif SERVERENGINE_COMPILER_CLANG || SERVERENGINE_COMPILER_GCC
    return __VERSION__;
#else
    return "unknown";
#endif
}

} // namespace serverengine::port
