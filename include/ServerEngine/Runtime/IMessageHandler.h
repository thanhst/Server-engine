#pragma once

#include <ServerEngine/Core/Buffer.h>

#include <string_view>

namespace serverengine::runtime {

class Session;

class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;

    virtual void on_session_started(Session& session);
    virtual void on_message(Session& session, const core::Buffer& message) = 0;
    virtual void on_session_stopped(Session& session);
    virtual void on_error(std::string_view message);
};

} // namespace serverengine::runtime
