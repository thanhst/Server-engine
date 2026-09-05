#pragma once

#include <ServerEngine/Core/Buffer.h>
#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Runtime/ConnectionHub.h>
#include <ServerEngine/Runtime/IMessageHandler.h>
#include <ServerEngine/Runtime/ServerOptions.h>
#include <ServerEngine/Runtime/Session.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace serverengine::runtime {

namespace detail {
class SessionRegistry;
class TcpListener;
}

class Server final {
public:
    // handler and logger must outlive this server and all of its callbacks.
    Server(ServerOptions options, IMessageHandler& handler, core::Logger& logger);
    ~Server();

    // Lifecycle calls are serialized by the caller, outside handler callbacks.
    [[nodiscard]] bool start(std::string* error_message = nullptr);
    void stop();

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] const ServerOptions& options() const noexcept;
    [[nodiscard]] ConnectionHub& hub() noexcept;
    [[nodiscard]] const ConnectionHub& hub() const noexcept;

    void dispatch_session_started(Session& session);
    void dispatch_message(Session& session, const core::Buffer& message);
    void dispatch_session_stopped(Session& session);

private:
    friend class detail::TcpListener;

    void report_handler_error(std::string_view message);

    ServerOptions options_;
    IMessageHandler& handler_;
    core::Logger& logger_;
    std::atomic_bool running_{false};
    ConnectionHub hub_;
    // Listeners stop and release their sessions before the shared registry/hub.
    std::unique_ptr<detail::SessionRegistry> session_registry_;
    std::vector<std::unique_ptr<detail::TcpListener>> tcp_listeners_;
};

} // namespace serverengine::runtime
