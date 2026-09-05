#include "AppConfig.h"
#include "EchoHandler.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const char* description)
{
    if (!condition) {
        throw std::runtime_error(description);
    }
}

void test_config()
{
    serverengine::core::Config source;
    const auto defaults = serverengine::app::parse_app_config(source);
    check(defaults.server.listeners.size() == 1, "default listener");
    check(defaults.server.listeners.front().bind_endpoint.port == 8080, "default port");
    check(defaults.access.require_auth && defaults.access.token == "secret", "compatible default authentication");

    source.set("server.port", "65536");
    source.set("server.workers", "0");
    source.set("server.max_message_bytes", "-4");
    source.set("server.transport", "invalid");
    source.set("server.tcp_backend", "invalid");
    source.set("security.mode", "invalid");
    source.set("log.level", "invalid");
    const auto fallback = serverengine::app::parse_app_config(source);
    check(fallback.server.listeners.front().bind_endpoint.port == 8080, "invalid port fallback");
    check(fallback.server.worker_count == 1 && fallback.server.max_message_bytes == 1, "minimum numeric limits");
    check(fallback.warnings.size() == 4, "invalid enum warnings retained");
}

void test_protocol()
{
    using namespace serverengine;
    core::LoggerOptions logging;
    logging.minimum_level = core::LogLevel::Off;
    logging.console_enabled = false;
    core::Logger logger(logging);
    runtime::ConnectionHub hub;
    const net::Endpoint endpoint{"127.0.0.1", 12345};
    hub.add_session(1, net::TransportKind::Tcp, endpoint);
    std::vector<std::string> replies;
    runtime::Session session(1, net::TransportKind::Tcp, endpoint,
        [&replies](runtime::SessionId, const core::Buffer& bytes, std::string*) {
            replies.push_back(bytes.to_text());
            return true;
        }, &hub);
    app::EchoHandler handler(logger, {true, "test-token"});
    handler.set_hub(hub);
    handler.on_session_started(session);
    check(replies.back() == "AUTH REQUIRED\n", "greeting unchanged");

    const auto request = [&](std::string_view text) {
        replies.clear();
        handler.on_message(session, core::Buffer::from_text(text));
        check(replies.size() == 1, "one response per command");
        return replies.front();
    };
    check(request("hello") == "ERROR auth required\n", "echo requires authentication");
    check(request("WHO") == "ERROR auth required\n", "WHO requires authentication");
    check(request("AUTH") == "AUTH ERROR usage: AUTH <user> <token>\n", "AUTH usage");
    check(request("AUTH alice wrong") == "AUTH DENIED\n", "wrong token rejected");
    check(!session.is_authenticated(), "failed AUTH leaves session unauthenticated");
    check(request("\xEF\xBB\xBF" "AUTH alice test-token") == "AUTH OK\n", "UTF-8 BOM accepted");
    check(session.user_name() == "alice", "authenticated identity stored in hub");
    check(request(" \thello\r\n") == "echo: hello\n", "whitespace normalization unchanged");
    const auto who = request("WHO");
    check(who.rfind("WHO count=1\n", 0) == 0, "WHO header");
    check(who.find("auth=yes user=alice") != std::string::npos, "WHO identity");

    // A different session must not inherit the first client's identity.
    hub.add_session(2, net::TransportKind::Tcp, endpoint);
    check(!hub.is_authenticated(2), "authentication is per session");
    runtime::Session anonymous(2, net::TransportKind::Tcp, endpoint,
        [&replies](runtime::SessionId, const core::Buffer& bytes, std::string*) {
            replies.push_back(bytes.to_text());
            return true;
        }, &hub);
    app::EchoHandler public_handler(logger, {false, "test-token"});
    replies.clear();
    public_handler.on_session_started(anonymous);
    check(replies.empty(), "auth-disabled sessions have no auth greeting");
    public_handler.on_message(anonymous, core::Buffer::from_text("hello"));
    check(replies.back() == "echo: hello\n", "auth-disabled echo");
}

} // namespace

int main()
{
    try {
        test_config();
        test_protocol();
        std::cout << "PASS EchoApp\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
