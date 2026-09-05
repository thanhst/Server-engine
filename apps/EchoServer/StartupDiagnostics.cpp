#include "StartupDiagnostics.h"

#include <ServerEngine/Port/Platform.h>
#include <ServerEngine/Port/Process.h>
#include <ServerEngine/Security/EccProvider.h>

namespace serverengine::app {

void log_startup_diagnostics(core::Logger& logger, const AppConfig& config)
{
    const auto process = port::current_process_info();
    const auto& server = config.server;
    logger.info("OS: ", port::to_string(port::current_operating_system()));
    logger.info("CPU: ", port::to_string(port::current_cpu_architecture()));
    logger.info("Compiler: ", port::compiler_name(), ' ', port::compiler_version());
    logger.info("Build: ", port::is_debug_build() ? "Debug" : "Release");
    logger.info("PID: ", process.process_id);
    logger.info("Executable: ", port::path_to_utf8(process.executable_path));
    logger.info("Working directory: ", port::path_to_utf8(process.working_directory));
    logger.debug("Logger file: ", port::path_to_utf8(config.logging.file_path));
    for (const auto& listener : server.listeners) {
        logger.info("Server transport: ", net::to_string(listener.transport));
        logger.info("Server endpoint: ", net::to_string(listener.bind_endpoint));
    }
    logger.info("Worker count: ", server.worker_count);
    logger.info("Max sessions: ", server.max_sessions);
    logger.info("Max message bytes: ", server.max_message_bytes);
    logger.info("Idle timeout ms: ", server.idle_timeout_ms);
    logger.info("Receive timeout ms: ", server.receive_timeout_ms);
    logger.info("TCP no delay: ", server.tcp_no_delay ? "true" : "false");
    logger.info("TCP backend: ", net::to_string(server.tcp_backend));
    logger.info("Security mode: ", security::to_string(server.security_mode));
    logger.info("Access require auth: ", config.access.require_auth ? "true" : "false");

    if (config.check_ecc_provider) {
        const auto ecc = security::check_ecc_p256_provider();
        logger.info("ECC P-256 provider: ", ecc.available ? "available" : "unavailable",
            " provider=", ecc.provider_name, " detail=", ecc.detail);
    }
    logger.info("Storage: ", config.storage.enabled ? "enabled" : "disabled",
        " provider=", config.storage.provider, " pool_size=", config.storage.pool_size);
    logger.info("Distribution: ", config.distribution.enabled ? "enabled" : "disabled",
        " node_id=", config.distribution.node_id,
        " advertised_endpoint=", config.distribution.advertised_endpoint);
    if (config.storage.enabled || config.distribution.enabled) {
        logger.warning("Storage/distribution settings are placeholders; no driver is implemented");
    }
}

} // namespace serverengine::app
