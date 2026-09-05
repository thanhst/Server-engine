#pragma once

#include <ServerEngine/C/ServerEngine.h>
#include "Runtime/Host/ServerHost.h"

namespace serverengine::abi {

bool decode(const se_server_options* source, runtime::host::HostOptions& result, std::string& error);
bool decode(const se_listener_options* source, net::ListenerConfig& result, std::string& error);

} // namespace serverengine::abi
