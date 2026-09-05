#pragma once

#include "Driver.h"

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace serverengine::data::sql {

// One worker owns the driver; application threads only touch immutable results.
// Accepted jobs stay in requests_ through delivery until explicit release.
class Service {
public:
    explicit Service(Options options);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    std::uint64_t submit(const se_sql_statement* statements, std::uint32_t count);
    se_status poll(se_sql_result& output, std::uint32_t timeout_ms);
    template<class Reader>
    se_status read_result(std::uint64_t id, Reader&& reader)
    {
        // Copy into caller buffers while holding the reservation's owner lock.
        // Concurrent release cannot free/reuse its budget during an active copy.
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = requests_.find(id);
        if (found == requests_.end() || found->second->state != Request::State::Delivered)
            throw SqlError(SE_INVALID_HANDLE, "SQL result not delivered, released or unknown");
        return reader(found->second->result);
    }
    void release(std::uint64_t id);
    void stop();

private:
    void run() noexcept;
    std::shared_ptr<Request> find_state(Request::State state) const;

    Options options_;
    std::unique_ptr<Driver> driver_;
    std::mutex lifecycle_mutex_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::uint64_t, std::shared_ptr<Request>> requests_;
    std::uint64_t next_id_{1};
    std::uint64_t reserved_bytes_{};
    std::atomic<bool> stopping_{false};
    bool worker_finished_{};
    std::thread worker_;
};

std::uint64_t register_service(std::shared_ptr<Service> service);
std::shared_ptr<Service> find_service(std::uint64_t handle);
std::shared_ptr<Service> remove_service(std::uint64_t handle);

} // namespace serverengine::data::sql
