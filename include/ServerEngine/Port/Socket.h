#pragma once

#include <ServerEngine/Port/Platform.h>

#include <cstdint>
#include <string>

namespace serverengine::port {

#if SERVERENGINE_OS_WINDOWS
using NativeSocket = std::uintptr_t;
inline constexpr NativeSocket InvalidSocket = static_cast<NativeSocket>(~NativeSocket{0});
#else
using NativeSocket = int;
inline constexpr NativeSocket InvalidSocket = -1;
#endif

class SocketSystem final {
public:
    SocketSystem();
    ~SocketSystem();

    SocketSystem(const SocketSystem&) = delete;
    SocketSystem& operator=(const SocketSystem&) = delete;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] const std::string& error_message() const noexcept;

private:
    bool initialized_{false};
    std::string error_message_;
};

[[nodiscard]] bool is_valid_socket(NativeSocket socket) noexcept;
void close_socket(NativeSocket socket) noexcept;
[[nodiscard]] std::string socket_error_message();

} // namespace serverengine::port
