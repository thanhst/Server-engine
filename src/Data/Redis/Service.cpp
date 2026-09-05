#include "Service.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

namespace serverengine::data::redis {

void Completion::fail(se_status code, const char* description) noexcept
{
    status = code;
    value.clear();
    found = false;
    affected_count = 0;
    const auto length = (std::min)(std::strlen(description), message.size() - 1);
    std::memcpy(message.data(), description, length);
    message[length] = '\0';
}

Service::Service(Options options)
    : options_(std::move(options)), connection_(options_), worker_([this] { run(); })
{
}

Service::~Service()
{
    stop();
}

se_status Service::submit(std::uint32_t operation, const void* key, std::uint32_t key_size,
    const void* value, std::uint32_t value_size, std::uint64_t ttl_ms,
    std::uint64_t& request_id)
{
    request_id = 0;
    if (key == nullptr || key_size == 0 || key_size > options_.max_key_bytes
        || (value == nullptr && value_size != 0) || value_size > options_.max_value_bytes
        || ttl_ms > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
        return SE_INVALID_ARGUMENT;

    std::lock_guard lock(mutex_);
    if (stopping_) return SE_STOPPED;
    const auto bytes = static_cast<std::uint64_t>(key_size) + value_size;
    const auto reservation = operation == SE_REDIS_GET ? options_.max_value_bytes : 0;
    if (inflight_ >= options_.max_inflight_requests
        || bytes > options_.max_queue_bytes - queue_bytes_
        || reservation > options_.max_result_bytes - reserved_result_bytes_)
        return SE_BACKPRESSURE;
    if (next_id_ == 0) return SE_INVALID_STATE;

    Request request;
    const auto* key_bytes = static_cast<const std::uint8_t*>(key);
    request.key.assign(key_bytes, key_bytes + key_size);
    if (value_size != 0) {
        const auto* value_bytes = static_cast<const std::uint8_t*>(value);
        request.value.assign(value_bytes, value_bytes + value_size);
    }
    request.ttl_ms = ttl_ms;
    request.result_reservation = reservation;
    request.completion.operation = operation;
    request.completion.request_id = next_id_;
    pending_.push_back(std::move(request)); // Allocate before accepting the ID.
    request_id = next_id_++;
    queue_bytes_ += bytes;
    reserved_result_bytes_ += reservation;
    ++inflight_;
    changed_.notify_all();
    return SE_OK;
}

void Service::run() noexcept
{
    std::list<Request> running;
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_) {
                for (auto& request : pending_) {
                    request.completion.fail(SE_STOPPED, "Redis request cancelled before execution");
                    queue_bytes_ -= request.key.size() + request.value.size();
                    Bytes{}.swap(request.key);
                    Bytes{}.swap(request.value);
                }
                ready_.splice(ready_.end(), pending_);
                stopped_ = true;
                changed_.notify_all();
                return;
            }
            running.splice(running.end(), pending_, pending_.begin());
        }
        auto& request = running.front();
        try {
            connection_.execute(request);
        } catch (...) {
            // execute protects failures after a write starts; this is a final
            // allocation-safety boundary for formatting or connection setup.
            request.completion.fail(SE_INTERNAL_ERROR, "Redis client failed locally");
        }
        {
            std::lock_guard lock(mutex_);
            queue_bytes_ -= request.key.size() + request.value.size();
            Bytes{}.swap(request.key);
            Bytes{}.swap(request.value);
            ready_.splice(ready_.end(), running);
            changed_.notify_all();
        }
    }
}

se_status Service::poll(se_redis_result& result, void* value, std::uint32_t capacity,
    std::uint32_t timeout_ms)
{
    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
        [this] { return !ready_.empty() || stopped_; })) return SE_TIMEOUT;
    if (ready_.empty()) return SE_STOPPED;
    const auto& request = ready_.front();
    const auto& completion = request.completion;
    result = {};
    result.struct_size = sizeof(result);
    result.abi_version = SE_ABI_VERSION;
    result.request_id = completion.request_id;
    result.operation = completion.operation;
    result.status = completion.status;
    result.found = completion.found ? 1u : 0u;
    result.value_size = static_cast<std::uint32_t>(completion.value.size());
    result.affected_count = completion.affected_count;
    std::memcpy(result.message, completion.message.data(), completion.message.size());
    if (completion.value.size() > capacity) return SE_BUFFER_TOO_SMALL;
    if (!completion.value.empty()) std::memcpy(value, completion.value.data(), completion.value.size());
    reserved_result_bytes_ -= request.result_reservation;
    --inflight_;
    ready_.pop_front();
    return SE_OK;
}

void Service::stop()
{
    std::lock_guard stop_lock(stop_mutex_);
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        changed_.notify_all();
    }
    connection_.cancel();
    if (worker_.joinable()) worker_.join();
}

} // namespace serverengine::data::redis
