#pragma once

#include "ConnectionState.h"
#include "StreamTypes.h"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <algorithm>
#include <array>
#include <deque>
#include <type_traits>
#include <utility>

namespace serverengine::net::async {

// Wire format: four unsigned big-endian length bytes, then exactly that many
// payload bytes. TLS changes the channel, never the application framing.
template<class Stream>
class TcpConnection final : public ConnectionState {
public:
    template<class... StreamArguments>
    TcpConnection(WorkerContext& context, Peer peer, ListenerConfig config,
        std::shared_ptr<boost::asio::ssl::context> tls_context, StreamArguments&&... arguments)
        : ConnectionState(context, std::move(peer), std::move(config)),
          tls_context_(std::move(tls_context)), stream_(std::forward<StreamArguments>(arguments)...)
    {
    }

    void start() override
    {
        if constexpr (std::is_same_v<Stream, TlsStream>) {
            begin_handshake_deadline();
            stream_.async_handshake(boost::asio::ssl::stream_base::server,
                guarded_handler([self = self()](boost::system::error_code error) {
                    if (error) return self->fail(error, "TLS handshake");
                    self->begin_messages();
                }));
        } else {
            begin_messages();
        }
    }

    bool send(const core::Buffer& message, std::string* error) override
    {
        if (closed_ || !opened_) return set_error(error, "Session is not open");
        if (message.size() > context_.limits.max_message_bytes)
            return set_error(error, "Message exceeds the configured size limit");
        const auto wire_size = message.size() + header_.size();
        if (outgoing_.size() >= 1024 || wire_size > context_.limits.max_send_queue_bytes ||
            queued_bytes_ > context_.limits.max_send_queue_bytes - wire_size)
            return set_error(error, "Session send queue is full");

        auto frame = std::make_shared<std::vector<std::uint8_t>>(wire_size);
        const auto length = static_cast<std::uint32_t>(message.size());
        (*frame)[0] = static_cast<std::uint8_t>(length >> 24);
        (*frame)[1] = static_cast<std::uint8_t>(length >> 16);
        (*frame)[2] = static_cast<std::uint8_t>(length >> 8);
        (*frame)[3] = static_cast<std::uint8_t>(length);
        std::copy(message.bytes().begin(), message.bytes().end(), frame->begin() + 4);
        const bool writing = !outgoing_.empty();
        outgoing_.push_back(std::move(frame));
        queued_bytes_ += wire_size;
        if (!writing) write_next();
        return true;
    }

    void close() override
    {
        if (closed_) return;
        closed_ = true;
        close_stream(stream_);
        outgoing_.clear();
        queued_bytes_ = 0;
        remove_from_registry();
    }

private:
    std::shared_ptr<TcpConnection> self()
    {
        return std::static_pointer_cast<TcpConnection>(shared_from_this());
    }

    static bool set_error(std::string* error, const char* text)
    {
        if (error) *error = text;
        return false;
    }

    void begin_messages()
    {
        if (publish_open()) read_header();
    }

    void read_header()
    {
        boost::asio::async_read(stream_, boost::asio::buffer(header_),
            guarded_handler([self = self()](boost::system::error_code error, std::size_t) {
                if (error) return self->fail(error, "TCP read header");
                if (self->closed_) return;
                const auto& h = self->header_;
                const std::uint32_t length = (std::uint32_t(h[0]) << 24) |
                    (std::uint32_t(h[1]) << 16) | (std::uint32_t(h[2]) << 8) | h[3];
                if (length > self->context_.limits.max_message_bytes)
                    return self->reject("TCP frame exceeds the configured size limit");
                self->incoming_.resize(length);
                self->read_body();
            }));
    }

    void read_body()
    {
        boost::asio::async_read(stream_, boost::asio::buffer(incoming_),
            guarded_handler([self = self()](boost::system::error_code error, std::size_t) {
                if (error) return self->fail(error, "TCP read payload");
                if (self->closed_) return;
                core::Buffer message(std::move(self->incoming_));
                if (self->publish_message(message)) self->read_header();
            }));
    }

    void write_next()
    {
        const auto frame = outgoing_.front();
        try {
            boost::asio::async_write(stream_, boost::asio::buffer(*frame),
                guarded_handler([self = self(), frame](boost::system::error_code error, std::size_t) {
                    // Keep frame alive independently of the queue during cancel.
                    if (error) return self->fail(error, "TCP write");
                    if (self->closed_) return;
                    self->queued_bytes_ -= frame->size();
                    self->outgoing_.pop_front();
                    self->touch_idle_deadline();
                    if (!self->outgoing_.empty()) self->write_next();
                }));
        } catch (...) {
            // The queue already owns this frame. A failed initiation must not
            // leave it permanently queued with no outstanding write operation.
            close();
            throw;
        }
    }

    // Context outlives stream destruction (members are destroyed in reverse).
    std::shared_ptr<boost::asio::ssl::context> tls_context_;
    Stream stream_;
    std::array<std::uint8_t, 4> header_{};
    std::vector<std::uint8_t> incoming_;
    std::deque<std::shared_ptr<std::vector<std::uint8_t>>> outgoing_;
    std::size_t queued_bytes_{};
};

} // namespace serverengine::net::async
