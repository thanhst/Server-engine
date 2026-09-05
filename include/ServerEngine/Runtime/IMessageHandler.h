#pragma once

#include <ServerEngine/Core/Buffer.h>

#include <string_view>

namespace serverengine::runtime {

class Session;

// One instance may serve many connections concurrently. Keep per-client state
// in the Session/ConnectionHub; protect any mutable state shared by callbacks.
// Session is borrowed for the callback duration. Request shutdown through your
// application owner thread instead of calling Server::stop() in a callback.
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;

    virtual void on_session_started(Session& session);
    virtual void on_message(Session& session, const core::Buffer& message) = 0;
    virtual void on_session_stopped(Session& session);
    virtual void on_error(std::string_view message);
};

} // namespace serverengine::runtime
