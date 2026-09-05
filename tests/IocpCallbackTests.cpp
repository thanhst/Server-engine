#include "Net/Iocp/Connection.h"

#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

using serverengine::net::Endpoint;
using serverengine::net::iocp::Connection;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void disconnect_during_connect_waits_for_registration()
{
    Connection connection(1, INVALID_SOCKET, Endpoint{"127.0.0.1", 45001}, 64);

    // The accept path enters this scope before publishing the connection.
    require(connection.begin_callback(), "connected callback must be admitted");
    connection.close();
    require(!connection.request_disconnect_notification(),
        "idle close must not notify before connected callback completes registration");
    require(!connection.begin_callback(), "closed connection must reject new callbacks");
    require(connection.end_callback(), "connected callback exit must deliver the deferred disconnect");
}

void disconnect_during_message_waits_for_handler_return()
{
    Connection connection(2, INVALID_SOCKET, Endpoint{"127.0.0.1", 45002}, 64);
    require(connection.begin_callback(), "connected callback must be admitted");
    require(!connection.end_callback(), "normal callback exit must not notify a disconnect");

    require(connection.begin_callback(), "open connection must admit a message callback");
    // A send failure can call disconnect from inside the message handler.
    connection.close();
    require(!connection.request_disconnect_notification(), "disconnect must wait for the message handler");
    require(connection.end_callback(), "message callback exit must deliver the deferred disconnect");
    require(!connection.begin_callback(), "no message may begin after final disconnect notification");
}

void disconnect_without_active_callbacks_notifies_immediately()
{
    Connection connection(3, INVALID_SOCKET, Endpoint{"127.0.0.1", 45003}, 64);
    connection.close();
    require(connection.is_closing(), "close must mark the connection before notification");
    require(connection.request_disconnect_notification(), "idle connection must notify immediately");
    require(!connection.begin_callback(), "close before callback entry must deny admission");
}

void only_the_last_active_scope_delivers_disconnect()
{
    Connection connection(4, INVALID_SOCKET, Endpoint{"127.0.0.1", 45004}, 64);
    require(connection.begin_callback() && connection.begin_callback(), "open scopes must be admitted");
    connection.close();
    require(!connection.request_disconnect_notification(), "active scopes must defer notification");
    require(!connection.end_callback(), "one remaining scope must keep notification deferred");
    require(connection.end_callback(), "the last scope must deliver the single notification");
}

void close_racing_with_callback_exit_notifies_exactly_once()
{
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        Connection connection(5, INVALID_SOCKET, Endpoint{"127.0.0.1", 45005}, 64);
        require(connection.begin_callback(), "callback must begin before the race");
        std::promise<void> start;
        const auto ready = start.get_future().share();
        bool callback_notifies = false;
        std::thread callback_thread([&connection, &callback_notifies, ready, iteration] {
            ready.wait();
            if (iteration % 2 == 0) {
                std::this_thread::yield();
            }
            callback_notifies = connection.end_callback();
        });

        start.set_value();
        if (iteration % 2 != 0) {
            std::this_thread::yield();
        }
        connection.close();
        // Production removes the registry entry before requesting notification,
        // so precisely one disconnect caller reaches this method.
        const bool close_notifies = connection.request_disconnect_notification();
        callback_thread.join();

        require(close_notifies != callback_notifies,
            "close and callback exit must jointly produce exactly one disconnect notification");
        require(!connection.begin_callback(), "racing close must prevent future callback admission");
    }
}

} // namespace

int main()
{
    try {
        // INVALID_SOCKET exercises lifecycle ordering without opening sockets,
        // creating a completion port, or requiring a running server.
        disconnect_during_connect_waits_for_registration();
        disconnect_during_message_waits_for_handler_return();
        disconnect_without_active_callbacks_notifies_immediately();
        only_the_last_active_scope_delivers_disconnect();
        close_racing_with_callback_exit_notifies_exactly_once();
        std::cout << "PASS IocpCallbacks\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL IocpCallbacks: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
