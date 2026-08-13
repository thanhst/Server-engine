#include <ServerEngine/Core/Config.h>
#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Net/Endpoint.h>
#include <ServerEngine/Net/TcpBackend.h>
#include <ServerEngine/Net/TransportKind.h>
#include <ServerEngine/Port/Clock.h>
#include <ServerEngine/Port/Platform.h>
#include <ServerEngine/Port/Process.h>
#include <ServerEngine/Runtime/IMessageHandler.h>
#include <ServerEngine/Runtime/Server.h>
#include <ServerEngine/Runtime/ServerOptions.h>
#include <ServerEngine/Runtime/Session.h>
#include <ServerEngine/Security/CredentialVerifier.h>
#include <ServerEngine/Security/EccProvider.h>
#include <ServerEngine/Security/SecurityMode.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::atomic_bool g_stop_requested{false};

void handle_stop_signal(int)
{
    g_stop_requested = true;
}

[[nodiscard]] std::uint16_t to_port(int value, std::uint16_t fallback)
{
    if (value <= 0 || value > 65535) {
        return fallback;
    }

    return static_cast<std::uint16_t>(value);
}

[[nodiscard]] std::string trim_line(std::string value)
{
    constexpr std::string_view Utf8Bom = "\xEF\xBB\xBF";
    if (value.rfind(Utf8Bom, 0) == 0) {
        value.erase(0, Utf8Bom.size());
    }

    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }

    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }

    if (first > 0) {
        value.erase(0, first);
    }

    return value;
}

class EchoHandler final : public serverengine::runtime::IMessageHandler {
public:
    EchoHandler(serverengine::core::Logger& logger, bool require_auth, std::string access_token)
        : logger_(logger)
        , require_auth_(require_auth)
        , access_token_(std::move(access_token))
    {
    }

    void set_hub(serverengine::runtime::ConnectionHub* hub)
    {
        hub_ = hub;
    }

    void on_session_started(serverengine::runtime::Session& session) override
    {
        logger_.info(
            "App session opened id=",
            session.id(),
            " transport=",
            serverengine::net::to_string(session.transport()),
            " remote=",
            serverengine::net::to_string(session.remote_endpoint()));

        if (require_auth_) {
            static_cast<void>(session.send_text("AUTH REQUIRED\n"));
        }
    }

    void on_message(serverengine::runtime::Session& session, const serverengine::core::Buffer& message) override
    {
        const auto request_text = trim_line(message.to_text());
        logger_.info("App received message session=", session.id(), " payload='", request_text, "'");

        if (handle_auth_command(session, request_text)) {
            return;
        }

        if (request_text == "WHO") {
            send_who(session);
            return;
        }

        if (require_auth_ && !session.is_authenticated()) {
            logger_.warning("Rejected unauthenticated message session=", session.id());
            static_cast<void>(session.send_text("ERROR auth required\n"));
            return;
        }

        std::string error;
        const auto response = serverengine::core::Buffer::from_text("echo: " + request_text + "\n");
        if (!session.send(response, &error)) {
            logger_.error("App failed to send response session=", session.id(), " error=", error);
        } else {
            logger_.info("App sent response session=", session.id(), " payload='", response.to_text(), "'");
        }
    }

    void on_session_stopped(serverengine::runtime::Session& session) override
    {
        logger_.info("App session closed id=", session.id());
    }

    void on_error(std::string_view message) override
    {
        logger_.error("App handler error: ", message);
    }

private:
    bool handle_auth_command(serverengine::runtime::Session& session, const std::string& request_text)
    {
        std::istringstream input(request_text);
        std::string command;
        std::string user_name;
        std::string token;
        input >> command >> user_name >> token;

        if (command != "AUTH") {
            return false;
        }

        if (user_name.empty() || token.empty()) {
            logger_.warning("Invalid AUTH command session=", session.id());
            static_cast<void>(session.send_text("AUTH ERROR usage: AUTH <user> <token>\n"));
            return true;
        }

        if (!serverengine::security::constant_time_equals(token, access_token_)) {
            logger_.warning("AUTH denied session=", session.id(), " user=", user_name);
            static_cast<void>(session.send_text("AUTH DENIED\n"));
            return true;
        }

        if (!session.authenticate_user(user_name)) {
            logger_.error("AUTH failed to update hub session=", session.id(), " user=", user_name);
            static_cast<void>(session.send_text("AUTH ERROR\n"));
            return true;
        }

        logger_.info("AUTH accepted session=", session.id(), " user=", user_name);
        static_cast<void>(session.send_text("AUTH OK\n"));
        return true;
    }

    void send_who(serverengine::runtime::Session& session)
    {
        if (require_auth_ && !session.is_authenticated()) {
            static_cast<void>(session.send_text("ERROR auth required\n"));
            return;
        }

        if (hub_ == nullptr) {
            static_cast<void>(session.send_text("ERROR hub unavailable\n"));
            return;
        }

        std::ostringstream output;
        const auto sessions = hub_->snapshot();
        output << "WHO count=" << sessions.size() << '\n';
        for (const auto& item : sessions) {
            output << "id=" << item.id
                   << " transport=" << serverengine::net::to_string(item.transport)
                   << " remote=" << serverengine::net::to_string(item.remote_endpoint)
                   << " auth=" << (item.authenticated ? "yes" : "no")
                   << " user=" << (item.user_name.empty() ? "-" : item.user_name)
                   << " rx_messages=" << item.messages_received
                   << " tx_messages=" << item.messages_sent
                   << " rx_bytes=" << item.bytes_received
                   << " tx_bytes=" << item.bytes_sent
                   << '\n';
        }

        static_cast<void>(session.send_text(output.str()));
    }

    serverengine::core::Logger& logger_;
    serverengine::runtime::ConnectionHub* hub_{nullptr};
    bool require_auth_{true};
    std::string access_token_;
};

} // namespace

int main()
{
    namespace core = serverengine::core;
    namespace net = serverengine::net;
    namespace port = serverengine::port;
    namespace runtime = serverengine::runtime;

    std::signal(SIGINT, handle_stop_signal);
#if defined(SIGTERM)
    std::signal(SIGTERM, handle_stop_signal);
#endif

    const std::filesystem::path config_path = "config/serverengine.ini";

    core::Config config;
    std::string config_error;
    const bool config_loaded = config.load_file(config_path, &config_error);

    const auto configured_log_level = config.get_string_or("log.level", "debug");
    const auto parsed_log_level = core::parse_log_level(configured_log_level);

    core::LoggerOptions log_options;
    log_options.minimum_level = parsed_log_level.value_or(core::LogLevel::Info);
    log_options.console_enabled = config.get_bool_or("log.console", true);
    log_options.file_path = config.get_string_or("log.file", "logs/serverengine.log");
    log_options.append_file = config.get_bool_or("log.append", true);

    core::Logger logger(log_options);
    if (!logger.initialize()) {
        std::cerr << "Failed to initialize logger\n";
        return 1;
    }

    const auto configured_transport = config.get_string_or("server.transport", "tcp");
    const auto parsed_transport = net::parse_transport_kind(configured_transport);
    const auto configured_tcp_backend = config.get_string_or("server.tcp_backend", std::string(net::to_string(net::default_tcp_backend())));
    const auto parsed_tcp_backend = net::parse_tcp_backend(configured_tcp_backend);
    const auto server_port = to_port(config.get_int_or("server.port", 8080), 8080);
    const auto worker_count = static_cast<std::size_t>(std::max(1, config.get_int_or("server.workers", 4)));
    const auto max_sessions = static_cast<std::size_t>(std::max(1, config.get_int_or("server.max_sessions", 10000)));
    const auto max_message_bytes = static_cast<std::size_t>(std::max(1, config.get_int_or("server.max_message_bytes", 65536)));
    const auto idle_timeout_ms = static_cast<std::uint64_t>(std::max(1, config.get_int_or("server.idle_timeout_ms", 300000)));
    const auto receive_timeout_ms = std::max(1, config.get_int_or("server.receive_timeout_ms", 1000));
    const bool tcp_no_delay = config.get_bool_or("server.tcp_no_delay", true);
    const bool require_auth = config.get_bool_or("access.require_auth", true);
    const auto access_token = config.get_string_or("access.token", "secret");
    const auto configured_security_mode = config.get_string_or("security.mode", "token");
    const auto parsed_security_mode = serverengine::security::parse_security_mode(configured_security_mode);
    const bool check_ecc_provider = config.get_bool_or("security.ecc_p256_provider_check", true);

    runtime::ListenerOptions listener_options;
    listener_options.name = "main";
    listener_options.transport = parsed_transport.value_or(net::TransportKind::Tcp);
    listener_options.bind_endpoint.address = config.get_string_or("server.host", "0.0.0.0");
    listener_options.bind_endpoint.port = server_port;

    runtime::ServerOptions server_options;
    server_options.worker_count = worker_count;
    server_options.max_sessions = max_sessions;
    server_options.max_message_bytes = max_message_bytes;
    server_options.idle_timeout_ms = idle_timeout_ms;
    server_options.receive_timeout_ms = receive_timeout_ms;
    server_options.tcp_no_delay = tcp_no_delay;
    server_options.tcp_backend = parsed_tcp_backend.value_or(net::default_tcp_backend());
    server_options.security_mode = parsed_security_mode.value_or(serverengine::security::SecurityMode::Token);
    server_options.listeners.push_back(listener_options);

    runtime::StorageOptions storage_options;
    storage_options.enabled = config.get_bool_or("storage.enabled", false);
    storage_options.provider = config.get_string_or("storage.provider", "");
    storage_options.connection_string = config.get_string_or("storage.connection_string", "");
    storage_options.pool_size = static_cast<std::size_t>(std::max(1, config.get_int_or("storage.pool_size", 16)));

    runtime::DistributionOptions distribution_options;
    distribution_options.enabled = config.get_bool_or("distribution.enabled", false);
    distribution_options.node_id = config.get_string_or("distribution.node_id", "node-1");
    distribution_options.advertised_endpoint = config.get_string_or("distribution.advertised_endpoint", "");

    const auto process = port::current_process_info();
    const auto started_at = port::steady_milliseconds();

    logger.info("ServerEngine starting");
    if (config_loaded) {
        logger.info("Config loaded: ", port::path_to_utf8(config_path));
    } else {
        logger.warning("Config load failed: ", config_error, "; using defaults");
    }

    if (!parsed_log_level) {
        logger.warning("Invalid log.level '", configured_log_level, "', using ", core::to_string(log_options.minimum_level));
    }

    if (!parsed_transport) {
        logger.warning("Invalid server.transport '", configured_transport, "', using ", net::to_string(listener_options.transport));
    }

    if (!parsed_tcp_backend) {
        logger.warning("Invalid server.tcp_backend '", configured_tcp_backend, "', using ", net::to_string(server_options.tcp_backend));
    }

    if (!parsed_security_mode) {
        logger.warning("Invalid security.mode '", configured_security_mode, "', using ", serverengine::security::to_string(server_options.security_mode));
    }

    if (server_options.security_mode == serverengine::security::SecurityMode::EccP256) {
        logger.critical("security.mode=ecc-p256 is configured, but encrypted ECC handshake is not implemented yet");
        return 1;
    }

    logger.info("Config entries: ", config.size());
    logger.info("OS: ", port::to_string(port::current_operating_system()));
    logger.info("CPU: ", port::to_string(port::current_cpu_architecture()));
    logger.info("Compiler: ", port::compiler_name(), ' ', port::compiler_version());
    logger.info("Build: ", port::is_debug_build() ? "Debug" : "Release");
    logger.info("PID: ", process.process_id);
    logger.info("Executable: ", port::path_to_utf8(process.executable_path));
    logger.info("Working directory: ", port::path_to_utf8(process.working_directory));
    logger.debug("Logger file: ", port::path_to_utf8(log_options.file_path));
    logger.info("Server transport: ", net::to_string(listener_options.transport));
    logger.info("Server endpoint: ", net::to_string(listener_options.bind_endpoint));
    logger.info("Worker count: ", server_options.worker_count);
    logger.info("Max sessions: ", server_options.max_sessions);
    logger.info("Max message bytes: ", server_options.max_message_bytes);
    logger.info("Idle timeout ms: ", server_options.idle_timeout_ms);
    logger.info("Receive timeout ms: ", server_options.receive_timeout_ms);
    logger.info("TCP no delay: ", server_options.tcp_no_delay ? "true" : "false");
    logger.info("TCP backend: ", net::to_string(server_options.tcp_backend));
    logger.info("Security mode: ", serverengine::security::to_string(server_options.security_mode));
    logger.info("Access require auth: ", require_auth ? "true" : "false");

    if (check_ecc_provider) {
        const auto ecc_status = serverengine::security::check_ecc_p256_provider();
        logger.info(
            "ECC P-256 provider: ",
            ecc_status.available ? "available" : "unavailable",
            " provider=",
            ecc_status.provider_name,
            " detail=",
            ecc_status.detail);
    }

    logger.info(
        "Storage: ",
        storage_options.enabled ? "enabled" : "disabled",
        " provider=",
        storage_options.provider,
        " pool_size=",
        storage_options.pool_size);
    logger.info(
        "Distribution: ",
        distribution_options.enabled ? "enabled" : "disabled",
        " node_id=",
        distribution_options.node_id,
        " advertised_endpoint=",
        distribution_options.advertised_endpoint);

    EchoHandler handler(logger, require_auth, access_token);
    runtime::Server server(server_options, handler, logger);
    handler.set_hub(&server.hub());

    std::string server_error;
    if (!server.start(&server_error)) {
        logger.critical("Failed to start runtime: ", server_error);
        return 1;
    }

    logger.info("ServerEngine running. Press Ctrl+C to stop.");

    while (!g_stop_requested.load()) {
        port::sleep_for(std::chrono::milliseconds(250));
    }

    logger.info("Stop requested");
    server.stop();

    port::sleep_for(std::chrono::milliseconds(1));
    logger.info("Clock elapsed: ", port::steady_milliseconds() - started_at, " ms");
    logger.info("ServerEngine stopped");

    return 0;
}
