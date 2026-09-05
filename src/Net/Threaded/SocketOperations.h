#pragma once

#include <ServerEngine/Net/TcpTypes.h>
#include <ServerEngine/Port/Socket.h>

#include <string>

namespace serverengine::net::threaded {

// The blocking backend's only OS-specific boundary. No native socket headers
// need to leak into the public server API or the connection lifecycle code.
[[nodiscard]] port::NativeSocket open_listener(const Endpoint& endpoint, std::string* error_message);
// Waits at most 100 ms: positive means ready, zero means timeout, negative
// means an OS error. The listener remains open until its accept thread exits.
[[nodiscard]] int wait_for_client(port::NativeSocket listener);
[[nodiscard]] port::NativeSocket accept_client(port::NativeSocket listener, Endpoint& remote_endpoint);
[[nodiscard]] bool last_accept_would_block() noexcept;
[[nodiscard]] bool configure_client_socket(port::NativeSocket socket, const TcpServerOptions& options, std::string* error_message);

[[nodiscard]] int receive_socket(port::NativeSocket socket, char* buffer, int size);
[[nodiscard]] int send_socket(port::NativeSocket socket, const char* buffer, int size);
[[nodiscard]] bool last_receive_was_timeout() noexcept;

// Interrupt blocking I/O without releasing the handle. Its owner closes it
// only after the threads using it have finished.
void shutdown_socket(port::NativeSocket socket) noexcept;

} // namespace serverengine::net::threaded
