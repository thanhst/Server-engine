#pragma once

#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Net/TcpTypes.h>
#include <ServerEngine/Port/Socket.h>

#include <atomic>
#include <mutex>
#include <string>

namespace serverengine::net::threaded {

// Owns one accepted socket. Registry entries, the receive thread, and callers
// sending data share this object, so a socket cannot be recycled during I/O.
class ClientConnection final {
public:
    ClientConnection(ConnectionId id, port::NativeSocket socket, Endpoint remote_endpoint);
    ~ClientConnection();

    ClientConnection(const ClientConnection&) = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;

    [[nodiscard]] ConnectionId id() const noexcept;
    [[nodiscard]] const Endpoint& remote_endpoint() const noexcept;

    [[nodiscard]] bool send(const core::Buffer& data, std::string* error_message);
    void disconnect() noexcept;

    // Runs on this client's receive thread. Empty means a normal disconnect
    // or idle timeout; otherwise the caller reports the returned error.
    [[nodiscard]] std::string receive_messages(
        const TcpServerOptions& options,
        const TcpServerCallbacks& callbacks,
        core::Logger& logger);

private:
    const ConnectionId id_;
    const port::NativeSocket socket_;
    const Endpoint remote_endpoint_;
    std::atomic_bool closed_{false};
    std::mutex send_mutex_;
};

} // namespace serverengine::net::threaded
