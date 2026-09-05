#include "ConnectionState.h"

#include <boost/asio/error.hpp>
#include <utility>

namespace serverengine::net::async {

ConnectionState::ConnectionState(WorkerContext& context, Peer peer, ListenerConfig config)
    : context_(context), peer_(std::move(peer)), config_(std::move(config)), deadline_(context.io)
{
}

void ConnectionState::arm_deadline(std::chrono::milliseconds duration)
{
    if (closed_) return;
    const auto generation = ++deadline_generation_;
    try {
        deadline_.expires_after(duration);
        deadline_.async_wait([self = shared_from_this(), generation](boost::system::error_code error) {
            // A successful old timeout can already be queued when activity
            // refreshes the timer. Cancellation alone cannot suppress that case.
            if (!error && !self->closed_ && generation == self->deadline_generation_)
                self->reject("Connection deadline exceeded");
        });
    } catch (...) {
        close();
        throw;
    }
}

void ConnectionState::begin_handshake_deadline()
{
    arm_deadline(std::chrono::milliseconds(config_.handshake_timeout_ms));
}

void ConnectionState::touch_idle_deadline()
{
    arm_deadline(std::chrono::milliseconds(context_.limits.idle_timeout_ms));
}

bool ConnectionState::publish_open()
{
    if (closed_) return false;
    if (!context_.notify_open(peer_)) {
        close();
        return false;
    }
    opened_ = true;
    touch_idle_deadline();
    return true;
}

bool ConnectionState::publish_message(const core::Buffer& message)
{
    if (closed_) return false;
    if (!context_.notify_message(peer_, message)) {
        close();
        return false;
    }
    touch_idle_deadline();
    return true;
}

void ConnectionState::remove_from_registry()
{
    // Current Asio exposes cancel() without an error_code overload. A timer
    // cancellation failure must not prevent removing the closed connection.
    try { (void)deadline_.cancel(); } catch (...) {}
    context_.remove_connection(peer_, opened_);
}

void ConnectionState::fail(const boost::system::error_code& error, const char* operation)
{
    if (closed_) return;
    if (error != boost::asio::error::operation_aborted && error != boost::asio::error::eof) {
        try {
            context_.report_error(peer_.listener_id, "Session " + std::to_string(peer_.session_id) +
                " " + operation + ": " + error.message());
        } catch (...) {
            // Diagnostic allocation must not prevent socket cleanup.
        }
    }
    close();
}

void ConnectionState::reject(const char* reason)
{
    if (closed_) return;
    try {
        context_.report_error(peer_.listener_id,
            "Session " + std::to_string(peer_.session_id) + ": " + reason);
    } catch (...) {
        // Keep rejection terminal even when formatting its diagnostic fails.
    }
    close();
}

} // namespace serverengine::net::async
