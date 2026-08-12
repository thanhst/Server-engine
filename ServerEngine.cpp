#include <ServerEngine/Core/Config.h>
#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Port/Clock.h>
#include <ServerEngine/Port/Platform.h>
#include <ServerEngine/Port/Process.h>

#include <chrono>
#include <filesystem>
#include <iostream>

int main()
{
    namespace core = serverengine::core;
    namespace port = serverengine::port;

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

    const auto server_port = config.get_int_or("server.port", 8080);
    const auto worker_count = config.get_int_or("server.workers", 4);
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

    logger.info("Config entries: ", config.size());
    logger.info("OS: ", port::to_string(port::current_operating_system()));
    logger.info("CPU: ", port::to_string(port::current_cpu_architecture()));
    logger.info("Compiler: ", port::compiler_name(), ' ', port::compiler_version());
    logger.info("Build: ", port::is_debug_build() ? "Debug" : "Release");
    logger.info("PID: ", process.process_id);
    logger.info("Executable: ", port::path_to_utf8(process.executable_path));
    logger.info("Working directory: ", port::path_to_utf8(process.working_directory));
    logger.debug("Logger file: ", port::path_to_utf8(log_options.file_path));
    logger.info("Server port: ", server_port);
    logger.info("Worker count: ", worker_count);

    port::sleep_for(std::chrono::milliseconds(1));
    logger.info("Clock elapsed: ", port::steady_milliseconds() - started_at, " ms");
    logger.info("ServerEngine stopped");

    return 0;
}
