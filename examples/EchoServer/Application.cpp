#include "Application.h"

#include "AppConfig.h"
#include "EchoHandler.h"
#include "StartupDiagnostics.h"

#include <ServerEngine/Port/Clock.h>
#include <ServerEngine/Port/Process.h>
#include <ServerEngine/Runtime/Server.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>

namespace serverengine::app {
namespace {

// Windows can deliver Ctrl+C on another thread. A lock-free atomic handles
// that case without taking a lock inside a POSIX signal handler either.
static_assert(std::atomic_bool::is_always_lock_free);
std::atomic_bool stop_requested{false};

void handle_stop_signal(int)
{
    // Signal handlers only set a flag. Logging and shutdown happen in run().
    stop_requested.store(true, std::memory_order_relaxed);
}

} // namespace

int run()
{
    stop_requested.store(false, std::memory_order_relaxed);
    std::signal(SIGINT, handle_stop_signal);
    std::signal(SIGTERM, handle_stop_signal);

    const std::filesystem::path config_path = "config/serverengine.ini";
    core::Config source;
    std::string config_error;
    const bool loaded = source.load_file(config_path, &config_error);
    const auto config = parse_app_config(source);

    core::Logger logger(config.logging);
    if (!logger.initialize()) {
        std::cerr << "Failed to initialize logger\n";
        return 1;
    }
    logger.info("ServerEngine starting");
    if (loaded) {
        logger.info("Config loaded: ", port::path_to_utf8(config_path));
    } else {
        logger.warning("Config load failed: ", config_error, "; using defaults");
    }
    for (const auto& warning : config.warnings) {
        logger.warning(warning);
    }
    if (config.server.security_mode == security::SecurityMode::EccP256) {
        logger.critical("security.mode=ecc-p256 is configured, but encrypted ECC handshake is not implemented yet");
        return 1;
    }
    logger.info("Config entries: ", source.size());
    log_startup_diagnostics(logger, config);

    // Locals are destroyed in reverse order: stop/join the server before its
    // callbacks lose their handler or logger. This is the ownership boundary.
    EchoHandler handler(logger, config.access);
    runtime::Server server(config.server, handler, logger);
    handler.set_hub(server.hub());

    const auto started_at = port::steady_milliseconds();
    std::string server_error;
    if (!server.start(&server_error)) {
        logger.critical("Failed to start runtime: ", server_error);
        return 1;
    }
    logger.info("ServerEngine running. Press Ctrl+C to stop.");
    while (!stop_requested.load(std::memory_order_relaxed)) {
        port::sleep_for(std::chrono::milliseconds(250));
    }
    logger.info("Stop requested");
    server.stop();
    logger.info("Clock elapsed: ", port::steady_milliseconds() - started_at, " ms");
    logger.info("ServerEngine stopped");
    return 0;
}

} // namespace serverengine::app
