#include "Server.h"

#if SERVERENGINE_OS_WINDOWS
#include "Operation.h"

#include <utility>

namespace serverengine::net::iocp {

void Server::worker_loop()
{
    for (;;) {
        DWORD transferred = 0;
        OVERLAPPED* overlapped = nullptr;
        const bool ok = completion_port_.wait(transferred, overlapped);
        if (overlapped == nullptr) {
            return;
        }

        // This is the single ownership handoff back from Windows to C++.
        std::unique_ptr<Operation> operation(static_cast<Operation*>(overlapped));
        auto connection = operation->connection;
        if (!ok || transferred == 0 || !running_ || connection->is_closing()) {
            disconnect(connection->id());
            continue;
        }

        if (operation->kind == OperationKind::Send) {
            std::string error;
            if (!connection->complete_send(std::move(operation), transferred, &error)) {
                report_error(error);
                disconnect(connection->id());
            }
        } else {
            connection->mark_active();
            deliver_received(connection, {operation->receive_buffer.data(), transferred});
        }
    }
}

void Server::deliver_received(const std::shared_ptr<Connection>& connection, std::string_view bytes)
{
    {
        CallbackScope callback(*this, connection);
        if (!callback || !running_) {
            return;
        }
        if (!connection->messages().append(bytes)) {
            report_error("connection closed: message exceeds max_message_bytes");
            disconnect(connection->id());
            return;
        }

        std::string line;
        while (connection->messages().try_pop(line)) {
            if (!running_ || connection->is_closing()) {
                return;
            }
            if (callbacks_.on_message) {
                callbacks_.on_message(connection->id(), connection->endpoint(), core::Buffer::from_text(line));
            }
        }
    }

    if (!running_ || connection->is_closing()) {
        return;
    }

    // Submit only after callbacks finish, so two workers cannot parse or deliver
    // messages for the same connection at the same time.
    std::string error;
    if (!connection->post_receive(completion_port_, &error)) {
        if (!connection->is_closing()) {
            report_error(error);
        }
        disconnect(connection->id());
    }
}

} // namespace serverengine::net::iocp
#endif
