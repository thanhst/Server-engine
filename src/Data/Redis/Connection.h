#pragma once

#include "Types.h"

#include <memory>

namespace serverengine::data::redis {

// The worker alone touches protocol/socket state. cancel() is the sole method
// allowed on another thread; it posts socket cancellation to the same executor.
class Connection {
public:
    explicit Connection(const Options& options);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    void execute(Request& request);
    void cancel() noexcept;
    static bool supported() noexcept;
    static bool valid_address(const std::string& address) noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace serverengine::data::redis
