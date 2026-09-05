#include "TcpListener.h"

#include "TcpConnection.h"
#include "HttpConnection.h"
#include "TlsContext.h"
#include "WebSocketConnection.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <utility>

namespace serverengine::net::async {
namespace {

class TcpListener final : public Listener, public std::enable_shared_from_this<TcpListener> {
public:
    TcpListener(WorkerContext& context, ListenerConfig config,
        std::shared_ptr<boost::asio::ssl::context> tls)
        : context_(context), config_(std::move(config)), tls_(std::move(tls)), acceptor_(context.io)
    {
        const auto address = boost::asio::ip::make_address(config_.bind_address);
        const boost::asio::ip::tcp::endpoint endpoint(address, config_.port);
        acceptor_.open(endpoint.protocol());
        if (address.is_v6()) acceptor_.set_option(boost::asio::ip::v6_only(true));
        // On Windows SO_REUSEADDR permits another process to bind an active
        // endpoint. Keep the platform default instead of silently enabling it.
        acceptor_.bind(endpoint);
        acceptor_.listen(boost::asio::socket_base::max_listen_connections);
    }

    void start() override { accept_next(); }

    void close() override
    {
        closed_ = true;
        boost::system::error_code ignored;
        acceptor_.cancel(ignored);
        acceptor_.close(ignored);
    }

private:
    void accept_next()
    {
        if (closed_ || context_.stopping) return;
        try {
            acceptor_.async_accept([self = shared_from_this()](boost::system::error_code error,
                                      boost::asio::ip::tcp::socket socket) {
                if (self->closed_ || self->context_.stopping) return;
                if (error) {
                    // Stop a failed listener instead of spinning on a persistent
                    // resource error. Close before allocating the diagnostic.
                    self->close();
                    self->context_.report_error(self->config_.id, "TCP accept: " + error.message());
                    return;
                }
                try { self->admit(std::move(socket)); }
                catch (const std::exception& failure) {
                    self->context_.report_error(self->config_.id, failure.what());
                }
                catch (...) {
                    self->close();
                    throw;
                }
                self->accept_next();
            });
        } catch (...) {
            close();
            throw;
        }
    }

    void admit(boost::asio::ip::tcp::socket socket)
    {
        if (context_.connections.size() >= context_.limits.max_connections) return;
        boost::system::error_code error;
        const auto remote = socket.remote_endpoint(error);
        if (error) return;
        socket.set_option(boost::asio::ip::tcp::no_delay(true), error);
        const auto id = context_.allocate_session_id();
        if (id == 0) {
            context_.report_error(config_.id, "Session ID space exhausted");
            return;
        }
        const Peer peer{id, config_.id, config_.protocol, remote.address().to_string(), remote.port()};
        std::shared_ptr<Connection> connection;
        if (config_.protocol == Protocol::Tcp) {
            if (tls_) connection = std::make_shared<TcpConnection<TlsStream>>(
                context_, peer, config_, tls_, std::move(socket), *tls_);
            else connection = std::make_shared<TcpConnection<PlainStream>>(
                context_, peer, config_, nullptr, std::move(socket));
        } else if (config_.protocol == Protocol::Http) {
            if (tls_) connection = std::make_shared<HttpConnection<TlsStream>>(
                context_, peer, config_, tls_, std::move(socket), *tls_);
            else connection = std::make_shared<HttpConnection<PlainStream>>(
                context_, peer, config_, nullptr, std::move(socket));
        } else {
            if (tls_) connection = std::make_shared<WebSocketConnection<TlsStream>>(
                context_, peer, config_, tls_, std::move(socket), *tls_);
            else connection = std::make_shared<WebSocketConnection<PlainStream>>(
                context_, peer, config_, nullptr, std::move(socket));
        }
        if (context_.add_connection(connection)) {
            try { connection->start(); }
            catch (...) {
                connection->close();
                throw;
            }
        }
    }

    WorkerContext& context_;
    ListenerConfig config_;
    std::shared_ptr<boost::asio::ssl::context> tls_;
    boost::asio::ip::tcp::acceptor acceptor_;
    bool closed_{false};
};

} // namespace

std::shared_ptr<Listener> make_tcp_listener(WorkerContext& context,
    const ListenerConfig& config, std::string* error)
{
    try {
        std::shared_ptr<boost::asio::ssl::context> tls;
        if (config.security == ChannelSecurity::Tls) {
            tls = make_tls_context(config, error);
            if (!tls) return {};
        }
        return std::make_shared<TcpListener>(context, config, std::move(tls));
    } catch (const std::exception& failure) {
        if (error) *error = std::string("Cannot open TCP listener: ") + failure.what();
        return {};
    }
}

} // namespace serverengine::net::async
