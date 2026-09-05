#include "EchoHandler.h"

#include <ServerEngine/Security/CredentialVerifier.h>

#include <sstream>
#include <utility>

namespace serverengine::app {
namespace {

std::string normalize_request(std::string text)
{
    constexpr std::string_view Utf8Bom = "\xEF\xBB\xBF";
    if (text.rfind(Utf8Bom, 0) == 0) {
        text.erase(0, Utf8Bom.size());
    }
    const auto last = text.find_last_not_of("\r\n \t");
    if (last == std::string::npos) {
        return {};
    }
    text.erase(last + 1);
    text.erase(0, text.find_first_not_of(" \t"));
    return text;
}

} // namespace

EchoHandler::EchoHandler(core::Logger& logger, AccessOptions access)
    : logger_(logger), access_(std::move(access))
{
}

void EchoHandler::set_hub(runtime::ConnectionHub& hub) noexcept
{
    hub_ = &hub;
}

void EchoHandler::on_session_started(runtime::Session& session)
{
    logger_.info("App session opened id=", session.id(),
        " transport=", net::to_string(session.transport()),
        " remote=", net::to_string(session.remote_endpoint()));
    if (access_.require_auth) {
        static_cast<void>(session.send_text("AUTH REQUIRED\n"));
    }
}

void EchoHandler::on_message(runtime::Session& session, const core::Buffer& message)
{
    const auto request = normalize_request(message.to_text());
    // Never log raw requests: AUTH contains a credential.
    logger_.info("App received message session=", session.id(), " bytes=", message.size());
    if (handle_auth_command(session, request)) {
        return;
    }
    if (access_.require_auth && !session.is_authenticated()) {
        logger_.warning("Rejected unauthenticated message session=", session.id());
        static_cast<void>(session.send_text("ERROR auth required\n"));
        return;
    }
    if (request == "WHO") {
        send_who(session);
        return;
    }

    std::string error;
    const auto response = core::Buffer::from_text("echo: " + request + "\n");
    if (!session.send(response, &error)) {
        logger_.error("App failed to send response session=", session.id(), " error=", error);
    } else {
        logger_.info("App sent response session=", session.id(), " bytes=", response.size());
    }
}

bool EchoHandler::handle_auth_command(runtime::Session& session, const std::string& request)
{
    std::istringstream input(request);
    std::string command;
    std::string user;
    std::string token;
    input >> command >> user >> token;
    if (command != "AUTH") {
        return false;
    }
    if (user.empty() || token.empty()) {
        logger_.warning("Invalid AUTH command session=", session.id());
        static_cast<void>(session.send_text("AUTH ERROR usage: AUTH <user> <token>\n"));
        return true;
    }
    if (!security::constant_time_equals(token, access_.token)) {
        logger_.warning("AUTH denied session=", session.id(), " user=", user);
        static_cast<void>(session.send_text("AUTH DENIED\n"));
        return true;
    }
    if (!session.authenticate_user(user)) {
        logger_.error("AUTH failed to update hub session=", session.id(), " user=", user);
        static_cast<void>(session.send_text("AUTH ERROR\n"));
        return true;
    }
    logger_.info("AUTH accepted session=", session.id(), " user=", user);
    static_cast<void>(session.send_text("AUTH OK\n"));
    return true;
}

void EchoHandler::send_who(runtime::Session& session)
{
    if (hub_ == nullptr) {
        static_cast<void>(session.send_text("ERROR hub unavailable\n"));
        return;
    }
    std::ostringstream output;
    const auto sessions = hub_->snapshot();
    output << "WHO count=" << sessions.size() << '\n';
    for (const auto& item : sessions) {
        output << "id=" << item.id
               << " transport=" << net::to_string(item.transport)
               << " remote=" << net::to_string(item.remote_endpoint)
               << " auth=" << (item.authenticated ? "yes" : "no")
               << " user=" << (item.user_name.empty() ? "-" : item.user_name)
               << " rx_messages=" << item.messages_received
               << " tx_messages=" << item.messages_sent
               << " rx_bytes=" << item.bytes_received
               << " tx_bytes=" << item.bytes_sent << '\n';
    }
    static_cast<void>(session.send_text(output.str()));
}

void EchoHandler::on_session_stopped(runtime::Session& session)
{
    logger_.info("App session closed id=", session.id());
}

void EchoHandler::on_error(std::string_view message)
{
    logger_.error("App handler error: ", message);
}

} // namespace serverengine::app
