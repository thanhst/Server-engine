#include "Connection.h"

#if SERVERENGINE_OS_WINDOWS
#include "Operation.h"

#include <ServerEngine/Port/Clock.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace serverengine::net::iocp {

Connection::Connection(ConnectionId id, SOCKET socket, Endpoint endpoint, std::size_t max_message_bytes)
    : id_(id), endpoint_(std::move(endpoint)), socket_(socket), messages_(max_message_bytes)
{
    mark_active();
}

Connection::~Connection()
{
    close();
}

void Connection::mark_active() noexcept
{
    last_activity_ms_ = port::steady_milliseconds();
}

void Connection::close() noexcept
{
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (closing_.exchange(true)) {
        return;
    }
    if (socket_ != INVALID_SOCKET) {
        ::shutdown(socket_, SD_BOTH);
        ::closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    send_queue_.clear();
}

bool Connection::begin_callback()
{
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (closing_) {
        return false;
    }
    ++active_callbacks_;
    return true;
}

bool Connection::end_callback() noexcept
{
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    --active_callbacks_;
    if (active_callbacks_ == 0 && disconnect_notification_pending_) {
        disconnect_notification_pending_ = false;
        return true;
    }
    return false;
}

bool Connection::request_disconnect_notification() noexcept
{
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (active_callbacks_ == 0) {
        return true;
    }
    disconnect_notification_pending_ = true;
    return false;
}

bool Connection::post_receive(CompletionPort& port, std::string* error_message)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (closing_) {
        set_error(error_message, "connection is closing");
        return false;
    }
    return submit(std::make_unique<Operation>(port, OperationKind::Receive, shared_from_this()), error_message);
}

bool Connection::send(CompletionPort& port, const core::Buffer& data, std::string* error_message)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (closing_) {
        set_error(error_message, "connection is closing");
        return false;
    }
    const auto* bytes = reinterpret_cast<const char*>(data.data());
    send_queue_.emplace_back(bytes, bytes + data.size());
    return send_in_progress_ || post_next_send(port, error_message);
}

bool Connection::post_next_send(CompletionPort& port, std::string* error_message)
{
    // Caller holds socket_mutex_. Each queued buffer follows the previous one
    // completely, even if Windows completes a send with only some of its bytes.
    if (send_queue_.empty()) {
        send_in_progress_ = false;
        return true;
    }
    auto operation = std::make_unique<Operation>(port, OperationKind::Send, shared_from_this());
    operation->send_buffer = std::move(send_queue_.front());
    send_queue_.pop_front();
    send_in_progress_ = true;
    if (!submit(std::move(operation), error_message)) {
        send_in_progress_ = false;
        return false;
    }
    return true;
}

bool Connection::complete_send(std::unique_ptr<Operation> operation, DWORD transferred, std::string* error_message)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (closing_) {
        return true;
    }
    mark_active();
    operation->send_offset += transferred;
    if (operation->send_offset < operation->send_buffer.size()) {
        return submit(std::move(operation), error_message);
    }
    // Keep this completion alive until the next queued operation is submitted.
    return post_next_send(operation->port(), error_message);
}

bool Connection::submit(std::unique_ptr<Operation> operation, std::string* error_message)
{
    // Caller holds socket_mutex_. Reset OVERLAPPED when retrying a partial send.
    static_cast<OVERLAPPED&>(*operation) = {};
    const bool receiving = operation->kind == OperationKind::Receive;
    if (receiving) {
        operation->buffer.buf = operation->receive_buffer.data();
        operation->buffer.len = static_cast<ULONG>(operation->receive_buffer.size());
    } else {
        operation->buffer.buf = operation->send_buffer.data() + operation->send_offset;
        const auto remaining = operation->send_buffer.size() - operation->send_offset;
        operation->buffer.len = static_cast<ULONG>((std::min)(remaining,
            static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())));
    }

    // A completion can arrive before WSARecv/WSASend returns. Transfer ownership
    // BEFORE submitting; successful submission must never touch this pointer again.
    Operation* submitted = operation.release();
    DWORD flags = 0;
    const int result = receiving
        ? ::WSARecv(socket_, &submitted->buffer, 1, nullptr, &flags, submitted, nullptr)
        : ::WSASend(socket_, &submitted->buffer, 1, nullptr, 0, submitted, nullptr);
    if (result == SOCKET_ERROR) {
        const auto error = ::WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            // Immediate failure generates no completion packet; ownership returns.
            std::unique_ptr<Operation> failed(submitted);
            set_error(error_message, std::string(receiving ? "WSARecv() failed: " : "WSASend() failed: ")
                + windows_error_message(static_cast<DWORD>(error)));
            return false;
        }
    }
    return true;
}

} // namespace serverengine::net::iocp
#endif
