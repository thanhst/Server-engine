#pragma once

#include "ConnectionState.h"
#include "HttpRequestCodec.h"
#include "HttpResponseValidation.h"
#include "StreamTypes.h"

#include <boost/beast/http.hpp>
#include <optional>
#include <type_traits>
#include <utility>

namespace serverengine::net::async {

// Network parses HTTP; the host owns routes and response content. Only one
// request is dispatched at a time. Pipelined bytes remain in the bounded Beast
// buffer until the previous response completes, preserving response order.
template<class Stream>
class HttpConnection final : public ConnectionState {
    using Body = boost::beast::http::vector_body<std::uint8_t>;
    using Parser = boost::beast::http::request_parser<Body>;

public:
    template<class... Arguments>
    HttpConnection(WorkerContext& context, Peer peer, ListenerConfig config,
        std::shared_ptr<boost::asio::ssl::context> tls_context, Arguments&&... arguments)
        : ConnectionState(context, std::move(peer), std::move(config)),
          tls_context_(std::move(tls_context)), stream_(std::forward<Arguments>(arguments)...),
          incoming_(http_header_limit)
    {
    }

    void start() override
    {
        if constexpr (std::is_same_v<Stream, TlsStream>) {
            begin_handshake_deadline();
            stream_.async_handshake(boost::asio::ssl::stream_base::server,
                guarded_handler([self = self()](boost::system::error_code error) {
                    if (error) return self->fail(error, "HTTPS handshake");
                    self->begin_requests();
                }));
        } else begin_requests();
    }

    bool send(const core::Buffer&, std::string* error) override
    {
        return set_error(error, "Use se_http_respond with a request ID for HTTP sessions");
    }

    bool respond_http(std::uint64_t request_id, const HttpResponse& response, std::string* error) override
    {
        namespace http = boost::beast::http;
        if (closed_ || !opened_ || request_id == 0 || pending_request_id_ != request_id)
            return set_error(error, "HTTP request is stale, already answered, or session is closed");
        std::size_t header_bytes{};
        if (!validate_http_response(response, context_.limits.max_message_bytes, header_bytes, error)) return false;
        const auto body_bytes = head_request_ ? 0 : response.body.size();
        if (header_bytes > context_.limits.max_send_queue_bytes ||
            body_bytes > context_.limits.max_send_queue_bytes - header_bytes)
            return set_error(error, "HTTP response exceeds the configured send queue limit");

        const bool no_content = response.status_code == 204 || response.status_code == 304;
        if (head_request_ || no_content || response.status_code == 205) {
            auto message = make_response<http::empty_body>(response);
            if (!no_content) message->content_length(response.body.size());
            write_response(std::move(message));
        } else {
            auto message = make_response<Body>(response);
            message->body() = response.body.bytes();
            message->prepare_payload();
            write_response(std::move(message));
        }
        return true;
    }

    void close() override
    {
        if (closed_) return;
        closed_ = true;
        pending_request_id_ = 0;
        close_stream(stream_);
        // Parser/input buffers may still belong to an outstanding read. Their
        // owning connection remains alive through its completion handler.
        remove_from_registry();
    }

private:
    std::shared_ptr<HttpConnection> self()
    {
        return std::static_pointer_cast<HttpConnection>(shared_from_this());
    }

    static bool set_error(std::string* error, const char* message)
    {
        if (error) *error = message;
        return false;
    }

    void begin_requests()
    {
        if (publish_open()) read_request();
    }

    void read_request()
    {
        parser_.emplace();
        parser_->header_limit(static_cast<std::uint32_t>(http_header_limit));
        parser_->body_limit(context_.limits.max_message_bytes);
        // A fixed deadline for the complete request prevents trickled bytes
        // from extending its lifetime forever (including on keep-alive).
        begin_handshake_deadline();
        boost::beast::http::async_read_header(stream_, incoming_, *parser_,
            guarded_handler([self = self()](boost::system::error_code error, std::size_t) {
                if (self->closed_) return;
                if (error) return self->read_failed(error);
                if (!self->validate_request_header()) return;
                self->read_body();
            }));
    }

    bool validate_request_header()
    {
        namespace http = boost::beast::http;
        const auto& request = parser_->get();
        if (request.version() != 11) return reject_http(505);
        if (request.count(http::field::host) != 1 || request[http::field::host].empty()) return reject_http(400);
        if (request.count(http::field::expect) != 0) return reject_http(417);
        if (request.method() == http::verb::connect || request.count(http::field::upgrade) != 0)
            return reject_http(501);
        const auto target = request.target();
        const bool origin_form = !target.empty() && target.front() == '/';
        const bool options_star = target == "*" && request.method() == http::verb::options;
        if ((!origin_form && !options_star) || target.size() > 2048 ||
            target.find('#') != boost::beast::string_view::npos ||
            request.method_string().empty() || request.method_string().size() > 32) return reject_http(400);
        return true;
    }

    void read_body()
    {
        if (parser_->is_done()) return dispatch_request();
        boost::beast::http::async_read(stream_, incoming_, *parser_,
            guarded_handler([self = self()](boost::system::error_code error, std::size_t) {
                if (self->closed_) return;
                if (error) return self->read_failed(error);
                self->dispatch_request();
            }));
    }

    void dispatch_request()
    {
        if (!validate_request_header()) return; // Also reject forbidden fields in trailers.
        const auto& request = parser_->get();
        std::string headers;
        for (const auto& field : request) {
            const auto name = field.name_string();
            const auto value = field.value();
            if (name.size() + value.size() + 4 > http_header_limit - headers.size()) {
                (void)reject_http(431);
                return;
            }
            headers.append(name.data(), name.size());
            headers.append(": ");
            headers.append(value.data(), value.size());
            headers.append("\r\n");
        }
        const auto total = http_envelope_bytes + request.method_string().size() +
            request.target().size() + headers.size() + request.body().size();
        if (total > context_.limits.max_message_bytes) {
            (void)reject_http(413);
            return;
        }
        const auto id = context_.allocate_http_request_id();
        if (!id) return reject("HTTP request ID space exhausted");
        const auto method = request.method_string();
        const auto target = request.target();
        const auto payload = encode_http_request(id, {method.data(), method.size()},
            {target.data(), target.size()}, headers, request.body());
        pending_request_id_ = id;
        head_request_ = request.method() == boost::beast::http::verb::head;
        request_keep_alive_ = request.keep_alive();
        if (!context_.notify_http_request(peer_, payload)) return close();
        parser_.reset();
        touch_idle_deadline(); // The application must answer before this expires.
    }

    void read_failed(const boost::system::error_code& error)
    {
        namespace http = boost::beast::http;
        if (error == http::error::end_of_stream || error == boost::asio::error::operation_aborted)
            return close();
        if (error == http::error::body_limit) (void)reject_http(413);
        else if (error == http::error::header_limit || error == http::error::buffer_overflow)
            (void)reject_http(431);
        else (void)reject_http(400);
    }

    bool reject_http(unsigned status)
    {
        namespace http = boost::beast::http;
        // Even generated error responses respect tiny configured send budgets.
        if (context_.limits.max_send_queue_bytes < 256) { close(); return false; }
        auto message = std::make_shared<http::response<http::empty_body>>(
            static_cast<http::status>(status), 11);
        message->keep_alive(false);
        message->content_length(0);
        write_response(std::move(message));
        return false;
    }

    template<class ResponseBody>
    std::shared_ptr<boost::beast::http::response<ResponseBody>> make_response(const HttpResponse& response)
    {
        namespace http = boost::beast::http;
        auto message = std::make_shared<http::response<ResponseBody>>(
            static_cast<http::status>(response.status_code), 11);
        message->keep_alive(request_keep_alive_ && !response.close_connection);
        message->set(http::field::content_type, response.content_type);
        for (const auto& header : response.headers) message->insert(header.name, header.value);
        return message;
    }

    template<class ResponseBody>
    void write_response(std::shared_ptr<boost::beast::http::response<ResponseBody>> message)
    {
        const bool keep_alive = message->keep_alive();
        pending_request_id_ = 0; // Invalidates duplicate responses before posting the write.
        try {
            touch_idle_deadline();
            boost::beast::http::async_write(stream_, *message,
                guarded_handler([self = self(), message, keep_alive](boost::system::error_code error, std::size_t) {
                    if (self->closed_) return;
                    if (error) return self->fail(error, "HTTP write response");
                    if (!keep_alive) return self->close();
                    self->read_request();
                }));
        } catch (...) {
            close();
            throw;
        }
    }

    std::shared_ptr<boost::asio::ssl::context> tls_context_;
    Stream stream_;
    boost::beast::flat_buffer incoming_;
    std::optional<Parser> parser_;
    std::uint64_t pending_request_id_{};
    bool head_request_{false};
    bool request_keep_alive_{false};
};

} // namespace serverengine::net::async
