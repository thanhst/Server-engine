#pragma once

#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Net/ITcpServer.h>

#include <memory>

namespace serverengine::net {

namespace iocp {
class Server;
}

// Public transport API. Windows handles, worker threads and buffers stay private.
class TcpIocpServer final : public ITcpServer {
public:
    explicit TcpIocpServer(core::Logger& logger);
    ~TcpIocpServer() override;

    TcpIocpServer(const TcpIocpServer&) = delete;
    TcpIocpServer& operator=(const TcpIocpServer&) = delete;

    [[nodiscard]] bool start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message = nullptr) override;
    void stop() override;

    [[nodiscard]] bool is_running() const noexcept override;
    [[nodiscard]] bool send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message = nullptr) override;
    void disconnect(ConnectionId connection_id) noexcept override;

private:
    std::unique_ptr<iocp::Server> server_;
};

} // namespace serverengine::net
