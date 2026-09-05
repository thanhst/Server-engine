#include "TcpServerImpl.h"

#include "SocketOperations.h"

#include <exception>
#include <utility>

namespace serverengine::net {

TcpServer::Impl::Impl(core::Logger& logger)
    : logger_(logger)
{
}

TcpServer::Impl::~Impl()
{
    stop();
}

bool TcpServer::Impl::start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message)
{
    if (running_) {
        return true;
    }
    if (!socket_system_.initialized()) {
        if (error_message != nullptr) {
            *error_message = socket_system_.error_message();
        }
        return false;
    }

    listen_socket_ = threaded::open_listener(options.bind_endpoint, error_message);
    if (!port::is_valid_socket(listen_socket_)) {
        return false;
    }

    options_ = std::move(options);
    callbacks_ = std::move(callbacks);
    running_ = true;
    try {
        accept_thread_ = std::thread(&Impl::accept_loop, this);
    } catch (const std::exception& error) {
        running_ = false;
        port::close_socket(listen_socket_);
        listen_socket_ = port::InvalidSocket;
        if (error_message != nullptr) {
            *error_message = std::string("starting TCP accept thread failed: ") + error.what();
        }
        return false;
    }
    logger_.info("TCP listening on ", to_string(options_.bind_endpoint));
    return true;
}

void TcpServer::Impl::stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    // Registration checks running_ under clients_mutex_, so no new client can
    // enter the registry after this snapshot. Interrupt I/O before joining:
    // an accept callback may itself be blocked sending to a client.
    decltype(clients_) clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients.swap(clients_);
    }
    for (const auto& [connection_id, client] : clients) {
        static_cast<void>(connection_id);
        client->disconnect();
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    // The bounded accept wait observes running_ without another thread closing
    // a socket underneath select/accept. No listener I/O remains after join.
    port::close_socket(listen_socket_);
    listen_socket_ = port::InvalidSocket;

    for (auto& worker : client_threads_) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
    client_threads_.clear();
    logger_.info("TCP listener stopped on ", to_string(options_.bind_endpoint));
}

bool TcpServer::Impl::is_running() const noexcept
{
    return running_;
}

bool TcpServer::Impl::send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message)
{
    std::shared_ptr<threaded::ClientConnection> client;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        const auto iterator = clients_.find(connection_id);
        if (iterator == clients_.end()) {
            if (error_message != nullptr) {
                *error_message = "connection not found";
            }
            return false;
        }
        client = iterator->second;
    }
    return client->send(data, error_message);
}

void TcpServer::Impl::disconnect(ConnectionId connection_id) noexcept
{
    std::shared_ptr<threaded::ClientConnection> client;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        const auto iterator = clients_.find(connection_id);
        if (iterator == clients_.end()) {
            return;
        }
        client = std::move(iterator->second);
        clients_.erase(iterator);
    }
    client->disconnect();
}

void TcpServer::Impl::accept_loop()
{
    while (running_) {
        const int ready = threaded::wait_for_client(listen_socket_);
        if (!running_) {
            break;
        }
        if (ready < 0) {
            report_error("waiting for TCP client failed: " + port::socket_error_message());
            continue;
        }
        if (ready == 0) {
            continue;
        }

        Endpoint remote_endpoint;
        const auto socket = threaded::accept_client(listen_socket_, remote_endpoint);
        if (!port::is_valid_socket(socket)) {
            if (running_ && !threaded::last_accept_would_block()) {
                report_error("accept() failed: " + port::socket_error_message());
            }
            continue;
        }

        reap_finished_clients();
        auto client = std::make_shared<threaded::ClientConnection>(next_connection_id_++, socket, std::move(remote_endpoint));
        if (!running_) {
            break;
        }
        if (callbacks_.on_accepting && !callbacks_.on_accepting(client->id(), client->remote_endpoint())) {
            continue;
        }
        std::string socket_error;
        if (!threaded::configure_client_socket(socket, options_, &socket_error)) {
            report_error(socket_error);
            continue;
        }

        bool at_capacity = false;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            if (!running_) {
                break;
            }
            at_capacity = clients_.size() >= options_.max_connections;
            if (!at_capacity) {
                clients_.emplace(client->id(), client);
            }
        }
        if (at_capacity) {
            report_error("connection rejected: max TCP connections reached");
            continue;
        }

        // Publish the application session before a receive callback can use it.
        // The callback may immediately send or disconnect through the registry.
        if (callbacks_.on_connected) {
            callbacks_.on_connected(client->id(), client->remote_endpoint());
        }
        launch_client(std::move(client));
    }
}

void TcpServer::Impl::launch_client(std::shared_ptr<threaded::ClientConnection> client)
{
    bool entry_added = false;
    try {
        // Allocate bookkeeping before a thread becomes joinable. In particular,
        // vector growth must never throw after a temporary thread has started.
        auto finished = std::make_shared<std::atomic_bool>(false);
        client_threads_.emplace_back();
        entry_added = true;
        auto& worker = client_threads_.back();
        worker.finished = finished;
        worker.thread = std::thread([this, client, finished] {
            run_client(client);
            finished->store(true);
        });
    } catch (const std::exception& error) {
        if (entry_added) {
            client_threads_.pop_back(); // Thread construction failed; it is not joinable.
        }
        disconnect(client->id());
        // on_connected already published the session, so every failure here
        // must pair it with the same cleanup notification as a normal exit.
        if (callbacks_.on_disconnected) {
            callbacks_.on_disconnected(client->id(), client->remote_endpoint());
        }
        report_error(std::string("starting TCP client thread failed: ") + error.what());
    }
}

void TcpServer::Impl::reap_finished_clients()
{
    for (auto iterator = client_threads_.begin(); iterator != client_threads_.end();) {
        if (iterator->finished->load()) {
            iterator->thread.join();
            iterator = client_threads_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void TcpServer::Impl::run_client(std::shared_ptr<threaded::ClientConnection> client)
{
    const auto error = client->receive_messages(options_, callbacks_, logger_);
    if (!error.empty()) {
        report_error(error);
    }
    disconnect(client->id());
    if (callbacks_.on_disconnected) {
        callbacks_.on_disconnected(client->id(), client->remote_endpoint());
    }
}

void TcpServer::Impl::report_error(std::string_view message)
{
    logger_.error("TCP error: ", message);
    if (callbacks_.on_error) {
        callbacks_.on_error(message);
    }
}

} // namespace serverengine::net
