#pragma once

#include <ServerEngine/Core/Buffer.h>
#include <ServerEngine/Net/Endpoint.h>
#include <ServerEngine/Net/TransportKind.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace serverengine::runtime {

using SessionId = std::uint64_t;

class ConnectionHub;

// A callback-scoped view of a connection. The send function and hub borrow
// server-owned state; do not retain this view after the handler returns.
class Session final {
public:
    using SendFunction = std::function<bool(SessionId session_id, const core::Buffer& data, std::string* error_message)>;

    Session(SessionId id, net::TransportKind transport, net::Endpoint remote_endpoint, SendFunction send_function, ConnectionHub* hub = nullptr);

    [[nodiscard]] SessionId id() const noexcept;
    [[nodiscard]] net::TransportKind transport() const noexcept;
    [[nodiscard]] const net::Endpoint& remote_endpoint() const noexcept;
    [[nodiscard]] bool is_authenticated() const;
    [[nodiscard]] std::string user_name() const;

    [[nodiscard]] bool authenticate_user(std::string_view user_name);
    [[nodiscard]] bool send(const core::Buffer& data, std::string* error_message = nullptr);
    [[nodiscard]] bool send_text(std::string_view text, std::string* error_message = nullptr);

private:
    SessionId id_{};
    net::TransportKind transport_{net::TransportKind::Tcp};
    net::Endpoint remote_endpoint_{};
    SendFunction send_function_;
    ConnectionHub* hub_{nullptr};
};

} // namespace serverengine::runtime
