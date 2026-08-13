#include <ServerEngine/Runtime/Session.h>

#include <ServerEngine/Runtime/ConnectionHub.h>

#include <utility>

namespace serverengine::runtime {

Session::Session(SessionId id, net::TransportKind transport, net::Endpoint remote_endpoint, SendFunction send_function, ConnectionHub* hub)
    : id_(id)
    , transport_(transport)
    , remote_endpoint_(std::move(remote_endpoint))
    , send_function_(std::move(send_function))
    , hub_(hub)
{
}

SessionId Session::id() const noexcept
{
    return id_;
}

net::TransportKind Session::transport() const noexcept
{
    return transport_;
}

const net::Endpoint& Session::remote_endpoint() const noexcept
{
    return remote_endpoint_;
}

bool Session::is_authenticated() const
{
    return hub_ != nullptr && hub_->is_authenticated(id_);
}

std::string Session::user_name() const
{
    if (hub_ == nullptr) {
        return {};
    }

    return hub_->user_name(id_);
}

bool Session::authenticate_user(std::string_view user_name)
{
    return hub_ != nullptr && hub_->authenticate(id_, user_name);
}

bool Session::send(const core::Buffer& data, std::string* error_message)
{
    if (!send_function_) {
        if (error_message != nullptr) {
            *error_message = "session has no send function";
        }
        return false;
    }

    const bool sent = send_function_(id_, data, error_message);
    if (sent && hub_ != nullptr) {
        hub_->record_sent(id_, data.size());
    }

    return sent;
}

bool Session::send_text(std::string_view text, std::string* error_message)
{
    return send(core::Buffer::from_text(text), error_message);
}

} // namespace serverengine::runtime
