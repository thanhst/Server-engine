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

namespace serverengine::net {
class ITcpServer;
}

namespace serverengine::runtime {

class Server final {
public:
    Server(ServerOptions options, IMessageHandler& handler, core::Logger& logger);
    ~Server();

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
    void report_handler_error(std::string_view message);

    ServerOptions options_;
    IMessageHandler& handler_;
    core::Logger& logger_;
    std::atomic_bool running_{false};
    ConnectionHub hub_;
    std::vector<std::unique_ptr<net::ITcpServer>> tcp_servers_;
};

} // namespace serverengine::runtime
