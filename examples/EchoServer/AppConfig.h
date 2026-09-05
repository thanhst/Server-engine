#pragma once

#include <ServerEngine/Core/Config.h>
#include <ServerEngine/Core/Logger.h>
#include <ServerEngine/Runtime/ServerOptions.h>

#include <string>
#include <vector>

namespace serverengine::app {

struct AccessOptions {
    bool require_auth{true};
    std::string token{"secret"};
};

// Application settings are separate from the reusable runtime API.
// Parse once at startup; workers only read the resulting values.
struct AppConfig {
    core::LoggerOptions logging;
    runtime::ServerOptions server;
    AccessOptions access;
    runtime::StorageOptions storage;
    runtime::DistributionOptions distribution;
    bool check_ecc_provider{true};
    std::vector<std::string> warnings;
};

[[nodiscard]] AppConfig parse_app_config(const core::Config& config);

} // namespace serverengine::app
