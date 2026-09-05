#include "ClientConnection.h"

#include "../Detail/LineFramer.h"
#include "SocketOperations.h"

#include <ServerEngine/Port/Clock.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace serverengine::net::threaded {

ClientConnection::ClientConnection(ConnectionId id, port::NativeSocket socket, Endpoint remote_endpoint)
    : id_(id), socket_(socket), remote_endpoint_(std::move(remote_endpoint))
{
}

ClientConnection::~ClientConnection()
{
    port::close_socket(socket_);
}

ConnectionId ClientConnection::id() const noexcept
{
    return id_;
}

const Endpoint& ClientConnection::remote_endpoint() const noexcept
{
    return remote_endpoint_;
}

bool ClientConnection::send(const core::Buffer& data, std::string* error_message)
{
    // A complete application write stays together even when several threads
    // send to the same client. Disconnect never waits for this mutex.
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (closed_) {
        if (error_message != nullptr) {
            *error_message = "connection is closed";
        }
        return false;
    }

    const char* cursor = reinterpret_cast<const char*>(data.data());
    auto remaining = data.size();
    const auto max_chunk_size = static_cast<std::size_t>((std::numeric_limits<int>::max)());
    while (remaining > 0) {
        const auto chunk_size = static_cast<int>((std::min)(remaining, max_chunk_size));
        const int sent = send_socket(socket_, cursor, chunk_size);
        if (sent <= 0) {
            if (error_message != nullptr) {
                *error_message = "send() failed: " + port::socket_error_message();
            }
            return false;
        }
        cursor += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

void ClientConnection::disconnect() noexcept
{
    if (!closed_.exchange(true)) {
        shutdown_socket(socket_);
    }
}

std::string ClientConnection::receive_messages(
    const TcpServerOptions& options,
    const TcpServerCallbacks& callbacks,
    core::Logger& logger)
{
    std::array<char, 4096> receive_buffer{};
    detail::LineFramer framer(options.max_message_bytes);
    auto last_activity_ms = port::steady_milliseconds();

    while (!closed_) {
        const int received = receive_socket(socket_, receive_buffer.data(), static_cast<int>(receive_buffer.size()));
        if (received > 0) {
            last_activity_ms = port::steady_milliseconds();
            if (!framer.append(std::string_view(receive_buffer.data(), static_cast<std::size_t>(received)))) {
                return "connection closed: message exceeds max_message_bytes";
            }

            std::string line;
            while (!closed_ && framer.try_pop(line)) {
                if (callbacks.on_message) {
                    callbacks.on_message(id_, remote_endpoint_, core::Buffer::from_text(line));
                }
            }
            continue;
        }

        if (received < 0 && !closed_) {
            if (!last_receive_was_timeout()) {
                return "recv() failed: " + port::socket_error_message();
            }
            const auto now_ms = port::steady_milliseconds();
            if (options.idle_timeout_ms > 0 && now_ms - last_activity_ms >= options.idle_timeout_ms) {
                logger.info("TCP idle timeout connection=", id_, " remote=", to_string(remote_endpoint_));
                break;
            }
            continue;
        }
        break;
    }
    return {};
}

} // namespace serverengine::net::threaded
