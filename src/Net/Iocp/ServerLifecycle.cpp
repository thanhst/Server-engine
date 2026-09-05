#include "Server.h"

#if SERVERENGINE_OS_WINDOWS
#include <algorithm>
#include <exception>
#include <utility>

namespace serverengine::net::iocp {

Server::Server(core::Logger& logger) : logger_(logger) {}

Server::~Server()
{
    stop();
}

bool Server::start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message)
{
    if (running_) {
        return true;
    }
    if (!socket_system_.initialized()) {
        set_error(error_message, socket_system_.error_message());
        return false;
    }

    SocketHandle listener(open_listener(options.bind_endpoint, error_message));
    if (listener.get() == INVALID_SOCKET || !completion_port_.open(error_message)) {
        return false;
    }

    options_ = std::move(options);
    callbacks_ = std::move(callbacks);
    listen_socket_ = listener.release();
    running_ = true;
    const auto worker_count = (std::max)(std::size_t{1}, options_.worker_count);
    try {
        worker_threads_.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            worker_threads_.emplace_back(&Server::worker_loop, this);
        }
        accept_thread_ = std::thread(&Server::accept_loop, this);
        idle_monitor_thread_ = std::thread(&Server::idle_monitor_loop, this);
    } catch (const std::exception& error) {
        set_error(error_message, std::string("failed to start IOCP threads: ") + error.what());
        stop();
        return false;
    }

    logger_.info("TCP IOCP listening on ", to_string(options_.bind_endpoint), " workers=", worker_count);
    return true;
}

void Server::stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    // First stop registration. Otherwise accept could add a connection after
    // close_all_connections took its snapshot and leave a live socket behind.
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    if (listen_socket_ != INVALID_SOCKET) {
        ::closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }
    if (idle_monitor_thread_.joinable()) {
        idle_monitor_thread_.join();
    }
    close_all_connections();

    // Workers must keep consuming canceled operations after running_ is false.
    // Only an explicit null completion tells them it is safe to exit.
    completion_port_.wait_until_drained();
    for (std::size_t index = 0; index < worker_threads_.size(); ++index) {
        completion_port_.wake_worker();
    }
    for (auto& worker : worker_threads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    worker_threads_.clear();
    completion_port_.close();
    logger_.info("TCP IOCP listener stopped on ", to_string(options_.bind_endpoint));
}

} // namespace serverengine::net::iocp
#endif
