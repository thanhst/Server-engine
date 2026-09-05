#pragma once

// Optional C++ convenience layer, compiled into the application. Only the C
// handles/buffers below cross ServerEngine.dll's boundary. No exported classes.
#include <ServerEngine/C/ServerEngine.h>
#include <ServerEngine/C/Sql.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace serverengine::sdk {

inline void require(se_status status, const se_error& error)
{
    if (status != SE_OK) throw std::runtime_error(error.message[0] ? error.message : "Engine operation failed");
}

struct NetworkEvent {
    se_event metadata{};
    std::vector<char> payload = std::vector<char>(4096);
    std::string_view bytes() const { return {payload.data(), metadata.payload_size}; }
};

class Network final {
public:
    explicit Network(const se_server_options& options)
    {
        if (se_get_abi_version() != SE_ABI_VERSION) throw std::runtime_error("Network ABI mismatch");
        se_error error{};
        require(se_server_create(&options, &handle_, &error), error);
    }
    ~Network() { if (handle_) se_server_destroy(handle_, nullptr); }
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;
    se_server_handle handle() const { return handle_; }
    std::uint64_t listen(const se_listener_options& options)
    {
        std::uint64_t listener{};
        se_error error{};
        require(se_server_add_listener(handle_, &options, &listener, &error), error);
        return listener;
    }
    void start() { se_error error{}; require(se_server_start(handle_, &error), error); }
    void stop() { se_error error{}; require(se_server_stop(handle_, &error), error); }
    se_status poll(NetworkEvent& event, std::uint32_t timeout_ms = 0)
    {
        se_event_init(&event.metadata);
        se_error error{};
        auto status = se_server_poll_event(handle_, &event.metadata, event.payload.data(),
            static_cast<std::uint32_t>(event.payload.size()), timeout_ms, &error);
        if (status == SE_BUFFER_TOO_SMALL) {
            event.payload.resize(event.metadata.payload_size);
            status = se_server_poll_event(handle_, &event.metadata, event.payload.data(),
                static_cast<std::uint32_t>(event.payload.size()), 0, &error);
        }
        if (status < 0) require(status, error);
        return status;
    }
private:
    se_server_handle handle_{};
};

namespace detail {
struct SqlOwner {
    se_sql_handle handle{};
    ~SqlOwner() { if (handle) se_sql_destroy(handle, nullptr); }
};
}

// A result retains its service owner. It can safely outlive the Sql object.
class SqlResult final {
public:
    SqlResult() = default;
    ~SqlResult() { reset(); }
    SqlResult(const SqlResult&) = delete;
    SqlResult& operator=(const SqlResult&) = delete;
    SqlResult(SqlResult&& other) noexcept : metadata(other.metadata), owner_(std::move(other.owner_))
    { other.metadata = {}; }
    SqlResult& operator=(SqlResult&& other) noexcept
    {
        if (this != &other) { reset(); metadata = other.metadata; owner_ = std::move(other.owner_); other.metadata = {}; }
        return *this;
    }
    void reset() noexcept
    {
        if (owner_ && metadata.request_id) se_sql_release_result(owner_->handle, metadata.request_id, nullptr);
        owner_.reset();
        metadata = {};
    }
    std::int64_t integer(std::uint32_t row, std::uint32_t column) const
    {
        ensure_result();
        se_sql_cell cell{};
        se_error error{};
        require(se_sql_get_cell(owner_->handle, metadata.request_id, row, column, &cell, nullptr, 0, &error), error);
        if (cell.type != SE_SQL_INT64) throw std::runtime_error("Expected SQL integer");
        return cell.integer;
    }
    std::string text(std::uint32_t row, std::uint32_t column) const
    {
        ensure_result();
        se_sql_cell cell{};
        se_error error{};
        auto status = se_sql_get_cell(owner_->handle, metadata.request_id, row, column, &cell, nullptr, 0, &error);
        if (status != SE_BUFFER_TOO_SMALL) require(status, error);
        if (cell.type != SE_SQL_TEXT) throw std::runtime_error("Expected SQL text");
        std::string value(cell.size, '\0');
        require(se_sql_get_cell(owner_->handle, metadata.request_id, row, column, &cell,
            value.data(), static_cast<std::uint32_t>(value.size()), &error), error);
        return value;
    }
    se_sql_result metadata{};
private:
    friend class Sql;
    void ensure_result() const
    {
        if (!owner_ || !metadata.request_id) throw std::logic_error("SQL result is empty or released");
        if (metadata.status != SE_OK) throw std::runtime_error(metadata.message);
    }
    std::shared_ptr<detail::SqlOwner> owner_;
};

class Sql final {
public:
    explicit Sql(const se_sql_options& options) : owner_(std::make_shared<detail::SqlOwner>())
    {
        if (se_sql_get_abi_version() != SE_SQL_ABI_VERSION) throw std::runtime_error("SQL ABI mismatch");
        se_error error{};
        require(se_sql_create(&options, &owner_->handle, &error), error);
    }
    Sql(const Sql&) = delete;
    Sql& operator=(const Sql&) = delete;
    std::uint64_t submit(const se_sql_statement* statements, std::uint32_t count)
    {
        std::uint64_t request{};
        se_error error{};
        require(se_sql_submit(owner_->handle, statements, count, &request, &error), error);
        return request;
    }
    std::uint64_t query(std::string_view sql, const se_sql_parameter* parameters = nullptr, std::uint32_t count = 0)
    {
        if (sql.size() > UINT32_MAX) throw std::length_error("SQL text exceeds ABI size");
        const se_sql_statement statement{sql.data(), static_cast<std::uint32_t>(sql.size()), count, parameters};
        return submit(&statement, 1);
    }
    se_status poll(SqlResult& result, std::uint32_t timeout_ms = 0)
    {
        result.reset();
        se_error error{};
        se_sql_result_init(&result.metadata);
        const auto status = se_sql_poll(owner_->handle, &result.metadata, timeout_ms, &error);
        if (status == SE_OK) result.owner_ = owner_;
        else if (status < 0) require(status, error);
        return status;
    }
    void stop() { se_error error{}; require(se_sql_stop(owner_->handle, &error), error); }
    se_sql_handle handle() const { return owner_->handle; }
private:
    std::shared_ptr<detail::SqlOwner> owner_;
};

} // namespace serverengine::sdk
