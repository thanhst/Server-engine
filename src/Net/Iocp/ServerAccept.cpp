#include "Server.h"

#if SERVERENGINE_OS_WINDOWS
namespace serverengine::net::iocp {

void Server::accept_loop()
{
    while (running_) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listen_socket_, &readable);
        timeval timeout{0, 100000}; // Recheck shutdown at least every 100 ms.
        const int ready = ::select(0, &readable, nullptr, nullptr, &timeout);
        if (!running_) {
            return;
        }
        if (ready == SOCKET_ERROR) {
            report_error("select(listener) failed: " + port::socket_error_message());
            continue;
        }
        if (ready == 0) {
            continue;
        }

        sockaddr_in address{};
        int address_size = sizeof(address);
        SocketHandle socket(::accept(listen_socket_, reinterpret_cast<sockaddr*>(&address), &address_size));
        if (socket.get() == INVALID_SOCKET) {
            const auto error = ::WSAGetLastError();
            if (running_ && error != WSAEWOULDBLOCK) {
                report_error("accept() failed: " + windows_error_message(static_cast<DWORD>(error)));
            }
            continue;
        }
        if (!running_) {
            return;
        }

        const auto id = next_connection_id_.fetch_add(1);
        const auto endpoint = remote_endpoint(address);
        if (callbacks_.on_accepting && !callbacks_.on_accepting(id, endpoint)) {
            continue;
        }
        if (!running_) {
            return;
        }
        configure_client_socket(socket.get(), options_);

        bool at_capacity = false;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            at_capacity = connections_.size() >= options_.max_connections;
        }
        if (at_capacity) {
            report_error("connection rejected: max TCP IOCP connections reached");
            continue;
        }

        std::string error;
        if (!completion_port_.associate(socket.get(), &error)) {
            report_error(error);
            continue;
        }
        auto connection = std::make_shared<Connection>(id, socket.get(), endpoint, options_.max_message_bytes);
        static_cast<void>(socket.release());
        {
            // Reserve the connected callback before publication. Even if idle
            // timeout closes this connection now, disconnected must arrive last.
            CallbackScope callback(*this, connection);
            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                connections_.emplace(id, connection);
            }

            if (callbacks_.on_connected) {
                callbacks_.on_connected(id, endpoint);
            }
        }
        // on_connected is allowed to disconnect (for example, rejected login).
        if (!running_ || connection->is_closing()) {
            disconnect(id);
            continue;
        }
        if (!connection->post_receive(completion_port_, &error)) {
            if (!connection->is_closing()) {
                report_error(error);
            }
            disconnect(id);
        }
    }
}

} // namespace serverengine::net::iocp
#endif
