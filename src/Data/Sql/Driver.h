#pragma once

#include "Types.h"

#include <atomic>
#include <memory>

namespace serverengine::data::sql {

// Driver owns its connection. After construction execute is worker-thread only.
// Interrupt may be called concurrently, but the driver stays alive until join.
class Driver {
public:
    virtual ~Driver() = default;
    virtual void execute(Request& request, const std::atomic<bool>& stopping) noexcept = 0;
    virtual void interrupt() noexcept = 0;
};

std::unique_ptr<Driver> make_sqlite_driver(const Options& options);

} // namespace serverengine::data::sql
