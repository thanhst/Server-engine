#pragma once

#include "WorkerContext.h"

#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <utility>

namespace serverengine::net::async {

// Shared lifecycle only. Protocol parsing and writes stay in their own classes.
class ConnectionState : public Connection,
                        public std::enable_shared_from_this<ConnectionState> {
public:
    ConnectionState(WorkerContext& context, Peer peer, ListenerConfig config);
    [[nodiscard]] const Peer& peer() const noexcept override { return peer_; }

protected:
    // A completion must either schedule its next operation or close the peer.
    // An allocation/Asio initiation exception must never leave a live session
    // with no read in flight. The worker reports the exception after cleanup.
    template<class Handler>
    auto guarded_handler(Handler handler)
    {
        return [self = shared_from_this(), handler = std::move(handler)](auto... arguments) mutable {
            try { handler(arguments...); }
            catch (...) {
                self->close();
                throw;
            }
        };
    }

    void begin_handshake_deadline();
    void touch_idle_deadline();
    [[nodiscard]] bool publish_open();
    [[nodiscard]] bool publish_message(const core::Buffer& message);
    void remove_from_registry();
    void fail(const boost::system::error_code& error, const char* operation);
    void reject(const char* reason);

    WorkerContext& context_;
    Peer peer_;
    ListenerConfig config_;
    bool closed_{false};
    bool opened_{false};

private:
    void arm_deadline(std::chrono::milliseconds duration);
    boost::asio::steady_timer deadline_;
    std::uint64_t deadline_generation_{};
};

} // namespace serverengine::net::async
