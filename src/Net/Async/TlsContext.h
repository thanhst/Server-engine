#pragma once

#include <ServerEngine/Net/TransportService.h>

#include <boost/asio/ssl/context.hpp>

#include <memory>
#include <string>

namespace serverengine::net::async {

// One immutable server context is shared by the listener's accepted connections.
// TCP, WebSocket and HTTP use the same TLS 1.3 policy: ephemeral ECDH key exchange,
// an EC server certificate, and authenticated symmetric encryption for payloads.
// Returns nullptr and a safe diagnostic when configuration cannot be loaded.
// UTF-8 PEM paths support native Unicode paths on Windows.
[[nodiscard]] std::shared_ptr<boost::asio::ssl::context> make_tls_context(
    const ListenerConfig& config, std::string* error);

} // namespace serverengine::net::async
