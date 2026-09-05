#pragma once

#include "AppConfig.h"

namespace serverengine::app {

void log_startup_diagnostics(core::Logger& logger, const AppConfig& config);

} // namespace serverengine::app
