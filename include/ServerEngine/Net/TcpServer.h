#pragma once

#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Net/ITcpServer.h>
#include <memory>

namespace serverengine::net {

class TcpServer final : public ITcpServer {
public:
    explicit TcpServer(core::Logger& logger);
    ~TcpServer() override;

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    [[nodiscard]] bool start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message = nullptr) override;
    void stop() override;

    [[nodiscard]] bool is_running() const noexcept override;
    [[nodiscard]] bool send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message = nullptr) override;
    void disconnect(ConnectionId connection_id) noexcept override;

private:
    // Thread and socket details stay private to the threaded backend.
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace serverengine::net
