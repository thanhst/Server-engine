#pragma once

#include "CompletionPort.h"

#if SERVERENGINE_OS_WINDOWS
#include "../Detail/LineFramer.h"

#include <ServerEngine/Core/Buffer.h>

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace serverengine::net::iocp {

struct Operation;

// Owns one socket. Submission and close share a lock; callbacks never run while
// that lock is held. Only one receive and one send are in flight per connection.
class Connection final : public std::enable_shared_from_this<Connection> {
public:
    Connection(ConnectionId id, SOCKET socket, Endpoint endpoint, std::size_t max_message_bytes);
    ~Connection();

    [[nodiscard]] ConnectionId id() const noexcept { return id_; }
    [[nodiscard]] const Endpoint& endpoint() const noexcept { return endpoint_; }
    [[nodiscard]] bool is_closing() const noexcept { return closing_; }
    [[nodiscard]] std::uint64_t last_activity_ms() const noexcept { return last_activity_ms_; }
    void mark_active() noexcept;
    void close() noexcept;

    // Disconnect never waits for application code. Its notification is deferred
    // until the current connected/message callback finishes instead.
    [[nodiscard]] bool begin_callback();
    [[nodiscard]] bool end_callback() noexcept;
    [[nodiscard]] bool request_disconnect_notification() noexcept;

    [[nodiscard]] bool post_receive(CompletionPort& port, std::string* error_message);
    [[nodiscard]] bool send(CompletionPort& port, const core::Buffer& data, std::string* error_message);
    [[nodiscard]] bool complete_send(std::unique_ptr<Operation> operation, DWORD transferred, std::string* error_message);

    // Accessed only by the worker handling this connection's current receive.
    [[nodiscard]] detail::LineFramer& messages() noexcept { return messages_; }

private:
    [[nodiscard]] bool post_next_send(CompletionPort& port, std::string* error_message);
    [[nodiscard]] bool submit(std::unique_ptr<Operation> operation, std::string* error_message);

    const ConnectionId id_;
    const Endpoint endpoint_;
    SOCKET socket_;
    std::atomic_bool closing_{false};
    std::atomic<std::uint64_t> last_activity_ms_{0};
    std::mutex socket_mutex_;
    std::deque<std::vector<char>> send_queue_;
    bool send_in_progress_{false};
    detail::LineFramer messages_;
    std::mutex callbacks_mutex_;
    std::size_t active_callbacks_{0};
    bool disconnect_notification_pending_{false};
};

} // namespace serverengine::net::iocp
#endif
