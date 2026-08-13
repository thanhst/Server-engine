#include <ServerEngine/Port/Socket.h>

#if SERVERENGINE_OS_WINDOWS
#include <winsock2.h>
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <unistd.h>
#endif

namespace serverengine::port {

#if SERVERENGINE_OS_WINDOWS

namespace {

[[nodiscard]] SOCKET to_windows_socket(NativeSocket socket) noexcept
{
    return static_cast<SOCKET>(socket);
}

} // namespace

SocketSystem::SocketSystem()
{
    WSADATA data{};
    const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        error_message_ = "WSAStartup failed: " + std::to_string(result);
        return;
    }

    initialized_ = true;
}

SocketSystem::~SocketSystem()
{
    if (initialized_) {
        ::WSACleanup();
    }
}

void close_socket(NativeSocket socket) noexcept
{
    if (is_valid_socket(socket)) {
        ::closesocket(to_windows_socket(socket));
    }
}

std::string socket_error_message()
{
    const auto error = ::WSAGetLastError();
    char* buffer = nullptr;

    const DWORD length = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(error),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    if (length == 0 || buffer == nullptr) {
        return "socket error: " + std::to_string(error);
    }

    std::string message(buffer, length);
    ::LocalFree(buffer);
    return message;
}

#else

SocketSystem::SocketSystem()
    : initialized_(true)
{
}

SocketSystem::~SocketSystem() = default;

void close_socket(NativeSocket socket) noexcept
{
    if (is_valid_socket(socket)) {
        ::close(socket);
    }
}

std::string socket_error_message()
{
    return std::strerror(errno);
}

#endif

bool SocketSystem::initialized() const noexcept
{
    return initialized_;
}

const std::string& SocketSystem::error_message() const noexcept
{
    return error_message_;
}

bool is_valid_socket(NativeSocket socket) noexcept
{
    return socket != InvalidSocket;
}

} // namespace serverengine::port
