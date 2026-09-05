#include <ServerEngine/Net/TcpServer.h>

#include "Threaded/TcpServerImpl.h"

#include <utility>

namespace serverengine::net {

TcpServer::TcpServer(core::Logger& logger)
    : impl_(std::make_unique<Impl>(logger))
{
}

TcpServer::~TcpServer() = default;

bool TcpServer::start(TcpServerOptions options, TcpServerCallbacks callbacks, std::string* error_message)
{
    return impl_->start(std::move(options), std::move(callbacks), error_message);
}

void TcpServer::stop()
{
    impl_->stop();
}

bool TcpServer::is_running() const noexcept
{
    return impl_->is_running();
}

bool TcpServer::send(ConnectionId connection_id, const core::Buffer& data, std::string* error_message)
{
    return impl_->send(connection_id, data, error_message);
}

void TcpServer::disconnect(ConnectionId connection_id) noexcept
{
    impl_->disconnect(connection_id);
}

} // namespace serverengine::net
