#include "AppConfig.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace serverengine::app {
namespace {

std::uint16_t read_port(const core::Config& config)
{
    const int value = config.get_int_or("server.port", 8080);
    return value > 0 && value <= 65535 ? static_cast<std::uint16_t>(value) : 8080;
}

void warn_invalid(AppConfig& result, std::string_view key, const std::string& value, std::string_view fallback)
{
    result.warnings.push_back("Invalid " + std::string(key) + " '" + value + "', using " + std::string(fallback));
}

} // namespace

AppConfig parse_app_config(const core::Config& config)
{
    AppConfig result;

    const auto log_level = config.get_string_or("log.level", "debug");
    const auto parsed_level = core::parse_log_level(log_level);
    result.logging.minimum_level = parsed_level.value_or(core::LogLevel::Info);
    result.logging.console_enabled = config.get_bool_or("log.console", true);
    result.logging.file_path = config.get_string_or("log.file", "logs/serverengine.log");
    result.logging.append_file = config.get_bool_or("log.append", true);
    if (!parsed_level) {
        warn_invalid(result, "log.level", log_level, core::to_string(result.logging.minimum_level));
    }

    runtime::ListenerOptions listener;
    const auto transport = config.get_string_or("server.transport", "tcp");
    const auto parsed_transport = net::parse_transport_kind(transport);
    listener.transport = parsed_transport.value_or(net::TransportKind::Tcp);
    listener.bind_endpoint.address = config.get_string_or("server.host", "0.0.0.0");
    listener.bind_endpoint.port = read_port(config);
    if (!parsed_transport) {
        warn_invalid(result, "server.transport", transport, net::to_string(listener.transport));
    }
    result.server.listeners.push_back(std::move(listener));

    const auto backend = config.get_string_or("server.tcp_backend", std::string(net::to_string(net::default_tcp_backend())));
    const auto parsed_backend = net::parse_tcp_backend(backend);
    result.server.tcp_backend = parsed_backend.value_or(net::default_tcp_backend());
    if (!parsed_backend) {
        warn_invalid(result, "server.tcp_backend", backend, net::to_string(result.server.tcp_backend));
    }

    const auto security_mode = config.get_string_or("security.mode", "token");
    const auto parsed_security = security::parse_security_mode(security_mode);
    result.server.security_mode = parsed_security.value_or(security::SecurityMode::Token);
    if (!parsed_security) {
        warn_invalid(result, "security.mode", security_mode, security::to_string(result.server.security_mode));
    }

    // Preserve the existing fallback policy: numeric limits are at least one.
    result.server.worker_count = static_cast<std::size_t>(std::max(1, config.get_int_or("server.workers", 4)));
    result.server.max_sessions = static_cast<std::size_t>(std::max(1, config.get_int_or("server.max_sessions", 10000)));
    result.server.max_message_bytes = static_cast<std::size_t>(std::max(1, config.get_int_or("server.max_message_bytes", 65536)));
    result.server.idle_timeout_ms = static_cast<std::uint64_t>(std::max(1, config.get_int_or("server.idle_timeout_ms", 300000)));
    result.server.receive_timeout_ms = std::max(1, config.get_int_or("server.receive_timeout_ms", 1000));
    result.server.tcp_no_delay = config.get_bool_or("server.tcp_no_delay", true);
    result.access.require_auth = config.get_bool_or("access.require_auth", true);
    result.access.token = config.get_string_or("access.token", "secret");
    result.check_ecc_provider = config.get_bool_or("security.ecc_p256_provider_check", true);

    // These settings are retained for compatibility; no storage/cluster driver
    // is started by this sample. Startup diagnostics state that explicitly.
    result.storage.enabled = config.get_bool_or("storage.enabled", false);
    result.storage.provider = config.get_string_or("storage.provider", "");
    result.storage.connection_string = config.get_string_or("storage.connection_string", "");
    result.storage.pool_size = static_cast<std::size_t>(std::max(1, config.get_int_or("storage.pool_size", 16)));
    result.distribution.enabled = config.get_bool_or("distribution.enabled", false);
    result.distribution.node_id = config.get_string_or("distribution.node_id", "node-1");
    result.distribution.advertised_endpoint = config.get_string_or("distribution.advertised_endpoint", "");
    return result;
}

} // namespace serverengine::app
