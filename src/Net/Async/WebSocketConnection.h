#pragma once

#include "ConnectionState.h"
#include "BufferedStream.h"
#include "StreamTypes.h"

#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <deque>
#include <type_traits>
#include <utility>

namespace serverengine::net::async {

// Beast handles masking, fragmentation, control frames and protocol validation.
// Each application event represents one complete binary WebSocket message.
template<class Stream>
class WebSocketConnection final : public ConnectionState {
public:
    template<class... StreamArguments>
    WebSocketConnection(WorkerContext& context, Peer peer, ListenerConfig config,
        std::shared_ptr<boost::asio::ssl::context> tls_context, StreamArguments&&... arguments)
        : ConnectionState(context, std::move(peer), std::move(config)),
          tls_context_(std::move(tls_context)), socket_(std::forward<StreamArguments>(arguments)...),
          incoming_(context.limits.max_message_bytes)
    {
        socket_.read_message_max(context.limits.max_message_bytes);
        socket_.binary(true);
        socket_.next_layer().buffer() = boost::beast::flat_buffer(8192);
        upgrade_parser_.header_limit(8192);
        upgrade_parser_.body_limit(0);
    }

    void start() override
    {
        begin_handshake_deadline();
        if constexpr (std::is_same_v<Stream, TlsStream>) {
            socket_.next_layer().next_layer().async_handshake(boost::asio::ssl::stream_base::server,
                guarded_handler([self = self()](boost::system::error_code error) {
                    if (error) return self->fail(error, "WSS TLS handshake");
                    self->read_upgrade_request();
                }));
        } else {
            read_upgrade_request();
        }
    }

    bool send(const core::Buffer& message, std::string* error) override
    {
        if (closed_ || !opened_) return set_error(error, "Session is not open");
        if (message.size() > context_.limits.max_message_bytes)
            return set_error(error, "Message exceeds the configured size limit");
        // Account for frame overhead so empty messages cannot bypass the limit.
        const auto cost = message.size() + 16;
        if (outgoing_.size() >= 1024 || cost > context_.limits.max_send_queue_bytes ||
            queued_bytes_ > context_.limits.max_send_queue_bytes - cost)
            return set_error(error, "Session send queue is full");
        auto payload = std::make_shared<core::Buffer>(message);
        const bool writing = !outgoing_.empty();
        outgoing_.push_back(std::move(payload));
        queued_bytes_ += cost;
        if (!writing) write_next();
        return true;
    }

    void close() override
    {
        if (closed_) return;
        closed_ = true;
        close_stream(socket_);
        outgoing_.clear();
        queued_bytes_ = 0;
        remove_from_registry();
    }

private:
    std::shared_ptr<WebSocketConnection> self()
    {
        return std::static_pointer_cast<WebSocketConnection>(shared_from_this());
    }

    static bool set_error(std::string* error, const char* text)
    {
        if (error) *error = text;
        return false;
    }

    void read_upgrade_request()
    {
        // Read from the underlying stream into the adapter's buffer. HTTP
        // parsing consumes only the headers; extra frame bytes remain buffered.
        boost::beast::http::async_read_header(socket_.next_layer().next_layer(),
            socket_.next_layer().buffer(), upgrade_parser_,
            guarded_handler([self = self()](boost::system::error_code error, std::size_t) {
                if (error) return self->fail(error, "WebSocket upgrade headers");
                if (self->closed_) return;
                if (!boost::beast::websocket::is_upgrade(self->upgrade_parser_.get()))
                    return self->reject("Invalid WebSocket upgrade request");
                if (self->upgrade_parser_.get().target() != boost::beast::string_view(
                        self->config_.websocket_path.data(), self->config_.websocket_path.size()))
                    return self->reject("WebSocket path does not match the configured endpoint");
                self->accept_upgrade();
            }));
    }

    void accept_upgrade()
    {
        socket_.async_accept(upgrade_parser_.get(),
            guarded_handler([self = self()](boost::system::error_code error) {
                if (error) return self->fail(error, "WebSocket upgrade");
                if (self->publish_open()) self->read_message();
            }));
    }

    void read_message()
    {
        socket_.async_read(incoming_,
            guarded_handler([self = self()](boost::system::error_code error, std::size_t) {
                if (error == boost::beast::websocket::error::closed) return self->close();
                if (error) return self->fail(error, "WebSocket read");
                if (self->closed_) return;
                if (self->socket_.got_text())
                    return self->reject("This endpoint accepts binary WebSocket messages only");
                std::vector<std::uint8_t> bytes(self->incoming_.size());
                boost::asio::buffer_copy(boost::asio::buffer(bytes), self->incoming_.data());
                self->incoming_.consume(self->incoming_.size());
                const core::Buffer message(std::move(bytes));
                if (self->publish_message(message)) self->read_message();
            }));
    }

    void write_next()
    {
        const auto payload = outgoing_.front();
        try {
            socket_.async_write(boost::asio::buffer(payload->data(), payload->size()),
                guarded_handler([self = self(), payload](boost::system::error_code error, std::size_t) {
                    if (error) return self->fail(error, "WebSocket write");
                    if (self->closed_) return;
                    self->queued_bytes_ -= payload->size() + 16;
                    self->outgoing_.pop_front();
                    self->touch_idle_deadline();
                    if (!self->outgoing_.empty()) self->write_next();
                }));
        } catch (...) {
            close();
            throw;
        }
    }

    std::shared_ptr<boost::asio::ssl::context> tls_context_;
    boost::beast::websocket::stream<BufferedStream<Stream>> socket_;
    boost::beast::flat_buffer incoming_;
    boost::beast::http::request_parser<boost::beast::http::empty_body> upgrade_parser_;
    std::deque<std::shared_ptr<core::Buffer>> outgoing_;
    std::size_t queued_bytes_{};
};

} // namespace serverengine::net::async
