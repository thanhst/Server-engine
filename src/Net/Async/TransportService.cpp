#include <ServerEngine/Net/TransportService.h>

#include "Listener.h"
#include "TcpListener.h"
#include "UdpListener.h"
#include "WorkerContext.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#include <future>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>

namespace serverengine::net {
namespace {

bool set_error(std::string* error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

bool validate(const std::vector<ListenerConfig>& listeners,
    const ServiceLimits& limits, std::string* error)
{
    if (limits.worker_threads != 1)
        return set_error(error, "This transport version requires worker_threads = 1");
    if (limits.max_connections == 0 || limits.max_connections > 1000000)
        return set_error(error, "max_connections must be between 1 and 1000000");
    if (limits.max_message_bytes == 0 || limits.max_message_bytes > 16 * 1024 * 1024)
        return set_error(error, "max_message_bytes must be between 1 and 16 MiB");
    if (limits.max_send_queue_bytes < limits.max_message_bytes + 16)
        return set_error(error, "Send queue must fit one maximum message plus 16 bytes");
    if (limits.idle_timeout_ms == 0 || limits.idle_timeout_ms > 86400000)
        return set_error(error, "idle_timeout_ms must be between 1 and 86400000");
    if (listeners.empty() || listeners.size() > 1024)
        return set_error(error, "Configure between 1 and 1024 listeners");
    std::unordered_set<std::uint64_t> ids;
    for (const auto& listener : listeners) {
        if (listener.id == 0 || !ids.insert(listener.id).second)
            return set_error(error, "Listener IDs must be unique and nonzero");
        if (listener.port == 0 || listener.bind_address.empty() ||
            listener.bind_address.find('\0') != std::string::npos)
            return set_error(error, "Listener needs a numeric bind address and nonzero port");
        if (listener.protocol != Protocol::Tcp && listener.protocol != Protocol::Udp &&
            listener.protocol != Protocol::WebSocket && listener.protocol != Protocol::Http)
            return set_error(error, "Unsupported transport protocol");
        if (listener.security != ChannelSecurity::None && listener.security != ChannelSecurity::Tls)
            return set_error(error, "Unsupported channel security mode");
        if (listener.protocol == Protocol::Udp && listener.security != ChannelSecurity::None)
            return set_error(error, "Encrypted UDP is not implemented; TLS applies to TCP/WSS/HTTPS only");
        if (listener.handshake_timeout_ms == 0 || listener.handshake_timeout_ms > 600000)
            return set_error(error, "handshake_timeout_ms must be between 1 and 600000");
        if (listener.protocol == Protocol::WebSocket &&
            (listener.websocket_path.empty() || listener.websocket_path.front() != '/' ||
             listener.websocket_path.size() > 2048 ||
             listener.websocket_path.find('\0') != std::string::npos))
            return set_error(error, "WebSocket path must start with / and contain at most 2048 bytes");
    }
    return true;
}

} // namespace

class TransportService::Impl {
public:
    ~Impl() { stop(); }

    bool start(const std::vector<ListenerConfig>& configurations, const ServiceLimits& limits,
        TransportCallbacks callbacks, std::string* error)
    {
        std::lock_guard<std::mutex> lock(commands_);
        if (running_) return set_error(error, "Transport service is already running");
        if (poisoned_) return set_error(error, "Transport service requires recreation after forced shutdown");
        if (!validate(configurations, limits, error)) return false;

        context_.io.restart();
        context_.stopping = false;
        context_.limits = limits;
        context_.callbacks = std::move(callbacks);

        // Prepare every listener before any accept/receive operation is posted.
        // If one fails, destruction releases all earlier binds transactionally.
        std::vector<std::shared_ptr<async::Listener>> prepared;
        for (const auto& config : configurations) {
            auto listener = config.protocol == Protocol::Udp
                ? async::make_udp_listener(context_, config, error)
                : async::make_tcp_listener(context_, config, error);
            if (!listener) {
                for (const auto& item : prepared) item->close();
                context_.callbacks = {};
                return false;
            }
            prepared.push_back(std::move(listener));
        }
        listeners_ = std::move(prepared);
        work_.emplace(boost::asio::make_work_guard(context_.io));
        try {
            worker_ = std::thread([this] { run_worker(); });
            running_ = true;
            const bool started = invoke([this] {
                try {
                    for (const auto& listener : listeners_) listener->start();
                    return true;
                } catch (const std::exception& failure) {
                    context_.report_error(0, failure.what());
                    return false;
                }
            });
            if (!started) {
                stop_locked();
                return set_error(error, "Could not start asynchronous listeners");
            }
            if (error) error->clear();
            return true;
        } catch (const std::exception& failure) {
            if (running_) stop_locked();
            else {
                for (const auto& listener : listeners_) listener->close();
                listeners_.clear();
                work_.reset();
                context_.callbacks = {};
            }
            return set_error(error, std::string("Could not start transport worker: ") + failure.what());
        }
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(commands_);
        stop_locked();
    }

    bool send(std::uint64_t session_id, const core::Buffer& message, std::string* error)
    {
        std::lock_guard<std::mutex> lock(commands_);
        if (!running_) return set_error(error, "Transport service is not running");
        return invoke([this, session_id, &message, error] {
            const auto found = context_.connections.find(session_id);
            if (found == context_.connections.end()) return set_error(error, "Unknown session ID");
            // Retain ownership: a send initiation failure may close/remove it.
            const auto connection = found->second;
            return connection->send(message, error);
        });
    }

    bool disconnect(std::uint64_t session_id, std::string* error)
    {
        std::lock_guard<std::mutex> lock(commands_);
        if (!running_) return set_error(error, "Transport service is not running");
        return invoke([this, session_id, error] {
            const auto found = context_.connections.find(session_id);
            if (found == context_.connections.end()) return set_error(error, "Unknown session ID");
            const auto connection = found->second;
            connection->close();
            return true;
        });
    }

    bool respond_http(std::uint64_t session_id, std::uint64_t request_id,
        const HttpResponse& response, std::string* error)
    {
        std::lock_guard<std::mutex> lock(commands_);
        if (!running_) return set_error(error, "Transport service is not running");
        return invoke([this, session_id, request_id, &response, error] {
            const auto found = context_.connections.find(session_id);
            if (found == context_.connections.end()) return set_error(error, "Unknown session ID");
            const auto connection = found->second;
            return connection->respond_http(request_id, response, error);
        });
    }

private:
    template<class Operation>
    bool invoke(Operation operation)
    {
        // commands_ remains locked until the future completes: stop cannot join
        // the worker between posting a command and obtaining its result.
        auto task = std::make_shared<std::packaged_task<bool()>>(std::move(operation));
        auto result = task->get_future();
        boost::asio::post(context_.io, [task] { (*task)(); });
        return result.get();
    }

    void stop_locked()
    {
        if (!running_) return;
        try {
            (void)invoke([this] {
                context_.stopping = true;
                close_listeners_and_connections();
                work_.reset();
                return true;
            });
        } catch (...) {
            // Command allocation can fail during memory pressure. First stop
            // and join the worker, then close its resources on this thread;
            // never abandon a joinable thread or call user code during cleanup.
            context_.io.stop();
            if (worker_.joinable()) worker_.join();
            context_.callbacks = {};
            context_.stopping = true;
            close_listeners_and_connections();
            work_.reset();
            poisoned_ = true;
        }
        // Cancellation handlers retain buffers/sockets until completion. Drain
        // them naturally; io.stop() would strand pending handlers across restart.
        if (worker_.joinable()) worker_.join();
        listeners_.clear();
        context_.callbacks = {};
        running_ = false;
    }

    void close_listeners_and_connections()
    {
        for (const auto& listener : listeners_) listener->close();
        while (!context_.connections.empty()) {
            const auto connection = context_.connections.begin()->second;
            connection->close();
        }
    }

    void run_worker() noexcept
    {
        // A user callback cannot throw through WorkerContext. If an allocation
        // inside a handler fails, report and continue servicing close commands.
        for (;;) {
            try {
                context_.io.run();
                break;
            } catch (const std::exception& failure) {
                context_.report_error(0, failure.what());
            } catch (...) {
                context_.report_error(0, "Unexpected transport handler failure");
            }
        }
    }

    std::mutex commands_;
    async::WorkerContext context_;
    std::vector<std::shared_ptr<async::Listener>> listeners_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    std::thread worker_;
    bool running_{false};
    // Emergency cleanup leaves cancelled handlers in the stopped io_context.
    // They are destroyed with the service; never execute them in a new run.
    bool poisoned_{false};
};

TransportService::TransportService() : impl_(std::make_unique<Impl>()) {}
TransportService::~TransportService() = default;

bool TransportService::start(const std::vector<ListenerConfig>& listeners, const ServiceLimits& limits,
    TransportCallbacks callbacks, std::string* error)
{
    return impl_->start(listeners, limits, std::move(callbacks), error);
}

void TransportService::stop() { impl_->stop(); }

bool TransportService::send(std::uint64_t session_id, const core::Buffer& message, std::string* error)
{
    return impl_->send(session_id, message, error);
}

bool TransportService::disconnect(std::uint64_t session_id, std::string* error)
{
    return impl_->disconnect(session_id, error);
}

bool TransportService::respond_http(std::uint64_t session_id, std::uint64_t request_id,
    const HttpResponse& response, std::string* error)
{
    return impl_->respond_http(session_id, request_id, response, error);
}

} // namespace serverengine::net
