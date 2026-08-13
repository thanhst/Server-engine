#pragma once

#include <ServerEngine/Core/Buffer.h>
#include <ServerEngine/Net/TcpTypes.h>

#include <string>

namespace serverengine::net {

class ITcpServer {
public:
    virtual ~ITcpServer() = default;

    [[nodiscard]] virtual bool start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message = nullptr) = 0;
    virtual void stop() = 0;

    [[nodiscard]] virtual bool is_running() const noexcept = 0;
    [[nodiscard]] virtual bool send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message = nullptr) = 0;
    virtual void disconnect(ConnectionId connection_id) noexcept = 0;
};

} // namespace serverengine::net
