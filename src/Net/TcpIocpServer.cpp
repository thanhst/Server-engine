#include <ServerEngine/Net/TcpIocpServer.h>

#include "Iocp/Server.h"

#include <utility>

namespace serverengine::net {

TcpIocpServer::TcpIocpServer(core::Logger& logger)
{
#if SERVERENGINE_OS_WINDOWS
    server_ = std::make_unique<iocp::Server>(logger);
#else
    static_cast<void>(logger);
#endif
}

TcpIocpServer::~TcpIocpServer() = default;

bool TcpIocpServer::start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message)
{
#if SERVERENGINE_OS_WINDOWS
    return server_->start(std::move(options), std::move(callbacks), error_message);
#else
    static_cast<void>(options);
    static_cast<void>(callbacks);
    if (error_message != nullptr) {
        *error_message = "IOCP TCP backend is only available on Windows";
    }
    return false;
#endif
}

void TcpIocpServer::stop()
{
#if SERVERENGINE_OS_WINDOWS
    server_->stop();
#endif
}

bool TcpIocpServer::is_running() const noexcept
{
#if SERVERENGINE_OS_WINDOWS
    return server_->is_running();
#else
    return false;
#endif
}

bool TcpIocpServer::send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message)
{
#if SERVERENGINE_OS_WINDOWS
    return server_->send(connection_id, data, error_message);
#else
    static_cast<void>(connection_id);
    static_cast<void>(data);
    if (error_message != nullptr) {
        *error_message = "IOCP TCP backend is only available on Windows";
    }
    return false;
#endif
}

void TcpIocpServer::disconnect(ConnectionId connection_id) noexcept
{
#if SERVERENGINE_OS_WINDOWS
    server_->disconnect(connection_id);
#else
    static_cast<void>(connection_id);
#endif
}

} // namespace serverengine::net
