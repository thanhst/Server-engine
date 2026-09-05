#pragma once

#include "AppConfig.h"

#include <ServerEngine/Runtime/ConnectionHub.h>
#include <ServerEngine/Runtime/IMessageHandler.h>

namespace serverengine::app {

// The sample protocol lives here. The networking library never knows AUTH/WHO.
// One handler is shared by workers: per-user state lives in ConnectionHub.
class EchoHandler final : public runtime::IMessageHandler {
public:
    EchoHandler(core::Logger& logger, AccessOptions access);

    // Wire the hub before Server::start(); it must outlive all callbacks.
    void set_hub(runtime::ConnectionHub& hub) noexcept;
    void on_session_started(runtime::Session& session) override;
    void on_message(runtime::Session& session, const core::Buffer& message) override;
    void on_session_stopped(runtime::Session& session) override;
    void on_error(std::string_view message) override;

private:
    bool handle_auth_command(runtime::Session& session, const std::string& request);
    void send_who(runtime::Session& session);

    core::Logger& logger_;
    const AccessOptions access_;
    runtime::ConnectionHub* hub_{nullptr};
};

} // namespace serverengine::app
