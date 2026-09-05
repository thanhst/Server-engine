#include "Service.h"

#include <cstring>
#include <limits>

namespace serverengine::data::sql {

Service::Service(Options options) : options_(std::move(options)), driver_(make_sqlite_driver(options_))
{
    worker_ = std::thread([this] { run(); });
}

Service::~Service()
{
    stop();
}

std::uint64_t Service::submit(const se_sql_statement* statements, std::uint32_t count)
{
    // Admission and input copying share one lock: concurrent producers cannot
    // allocate an unbounded number of copied-but-not-admitted requests.
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_.load(std::memory_order_relaxed))
        throw SqlError(SE_STOPPED, "SQL service is stopped");
    if (requests_.size() >= options_.max_outstanding)
        throw SqlError(SE_BACKPRESSURE, "SQL capacity full; poll and release outstanding results");
    auto request = std::make_shared<Request>(copy_request(statements, count, options_));
    if (request->reserved_bytes > options_.memory_budget - reserved_bytes_)
        throw SqlError(SE_BACKPRESSURE, "SQL memory reservations full; poll and release results");
    if (next_id_ == (std::numeric_limits<std::uint64_t>::max)())
        throw SqlError(SE_INVALID_STATE, "SQL request ID space exhausted");
    request->id = next_id_++;
    requests_.emplace(request->id, request); // May throw before admission is committed.
    reserved_bytes_ += request->reserved_bytes;
    changed_.notify_all();
    return request->id;
}

std::shared_ptr<Request> Service::find_state(Request::State state) const
{
    for (const auto& entry : requests_)
        if (entry.second->state == state) return entry.second;
    return {};
}

void Service::run() noexcept
{
    for (;;) {
        std::shared_ptr<Request> request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait(lock, [this] {
                return stopping_.load(std::memory_order_relaxed) || find_state(Request::State::Queued);
            });
            request = find_state(Request::State::Queued);
            if (!request) {
                worker_finished_ = true;
                changed_.notify_all();
                return;
            }
            request->state = Request::State::Running;
        }
        if (stopping_.load(std::memory_order_relaxed))
            request->result.fail(SE_SQL_CANCELLED, "SQL request cancelled before execution");
        else
            driver_->execute(*request, stopping_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // No allocation here. Even allocation failure during execution has
            // a preallocated Result and guaranteed completion slot.
            request->state = Request::State::Complete;
            request.reset(); // Release worker ownership before callers can release the reservation.
        }
        changed_.notify_all();
    }
}

se_status Service::poll(se_sql_result& output, std::uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!changed_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
        return worker_finished_ || find_state(Request::State::Complete);
    })) return SE_TIMEOUT;
    auto request = find_state(Request::State::Complete);
    if (!request) return SE_STOPPED;
    const auto& result = request->result;
    output = {};
    output.struct_size = sizeof(output);
    output.abi_version = SE_SQL_ABI_VERSION;
    output.request_id = request->id;
    output.status = result.status;
    output.row_count = static_cast<std::uint32_t>(result.rows.size());
    output.column_count = static_cast<std::uint32_t>(result.columns.size());
    output.affected_rows = result.affected_rows;
    output.last_insert_id = result.last_insert_id;
    std::memcpy(output.message, result.message.data(), sizeof(output.message));
    request->state = Request::State::Delivered;
    return SE_OK;
}

void Service::release(std::uint64_t id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = requests_.find(id);
    if (found == requests_.end() || found->second->state != Request::State::Delivered)
        throw SqlError(SE_INVALID_HANDLE, "SQL result not delivered, released or unknown");
    reserved_bytes_ -= found->second->reserved_bytes;
    requests_.erase(found);
}

void Service::stop()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true, std::memory_order_relaxed);
    }
    if (worker_.joinable()) {
        driver_->interrupt();
        changed_.notify_all();
        worker_.join();
    }
}

} // namespace serverengine::data::sql
