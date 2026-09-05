#pragma once

#include "Listener.h"
#include "WorkerContext.h"

namespace serverengine::net::async {

// Opens/binds immediately; start() begins accepting after startup commits.
[[nodiscard]] std::shared_ptr<Listener> make_tcp_listener(WorkerContext& context,
    const ListenerConfig& config, std::string* error);

} // namespace serverengine::net::async
