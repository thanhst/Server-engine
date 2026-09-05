#pragma once

#include "Connection.h"

#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

namespace serverengine::data::redis {

// Admission reserves both a request slot and worst-case result bytes. A slow
// consumer receives backpressure; completed results are never silently dropped.
class Service {
public:
    explicit Service(Options options);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    se_status submit(std::uint32_t operation, const void* key, std::uint32_t key_size,
        const void* value, std::uint32_t value_size, std::uint64_t ttl_ms,
        std::uint64_t& request_id);
    se_status poll(se_redis_result& result, void* value, std::uint32_t capacity,
        std::uint32_t timeout_ms);
    void stop();
private:
    void run() noexcept;
    const Options options_;
    Connection connection_;
    std::mutex mutex_;
    std::mutex stop_mutex_;
    std::condition_variable changed_;
    std::list<Request> pending_;
    std::list<Request> ready_;
    std::uint64_t next_id_ = 1;
    std::uint64_t queue_bytes_ = 0;
    std::uint64_t reserved_result_bytes_ = 0;
    std::uint32_t inflight_ = 0;
    bool stopping_ = false;
    bool stopped_ = false;
    std::thread worker_;
};

} // namespace serverengine::data::redis
