#include <ServerEngine/Net/TcpBackend.h>

#include <ServerEngine/Port/Platform.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace serverengine::net {

namespace {

[[nodiscard]] std::string normalize(std::string_view value)
{
    std::string result(value.begin(), value.end());
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

} // namespace

std::string_view to_string(TcpBackend backend) noexcept
{
    switch (backend) {
    case TcpBackend::Threaded:
        return "threaded";
    case TcpBackend::Iocp:
        return "iocp";
    }

    return "threaded";
}

std::optional<TcpBackend> parse_tcp_backend(std::string_view value)
{
    const auto normalized = normalize(value);
    if (normalized == "threaded" || normalized == "thread") {
        return TcpBackend::Threaded;
    }

    if (normalized == "iocp" || normalized == "windows-iocp") {
        return TcpBackend::Iocp;
    }

    return std::nullopt;
}

TcpBackend default_tcp_backend() noexcept
{
#if SERVERENGINE_OS_WINDOWS
    return TcpBackend::Iocp;
#else
    return TcpBackend::Threaded;
#endif
}

} // namespace serverengine::net
