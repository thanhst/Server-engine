#pragma once

#include "Driver.h"

#include <sqlite3.h>

#include <chrono>

namespace serverengine::data::sql {

class SqliteDriver final : public Driver {
public:
    explicit SqliteDriver(const Options& options);
    ~SqliteDriver() override;
    void execute(Request& request, const std::atomic<bool>& stopping) noexcept override;
    void interrupt() noexcept override;

private:
    static int authorize(void*, int action, const char*, const char*, const char*, const char*) noexcept;
    static int progress(void* context) noexcept;
    static int busy(void* context, int attempts) noexcept;
    void execute_statement(const Statement& input, bool final, Result& result, std::uint64_t& bytes);
    void read_columns(sqlite3_stmt* statement, Result& result, std::uint64_t& bytes);
    void read_row(sqlite3_stmt* statement, Result& result, std::uint64_t& bytes);
    void account(std::uint64_t& bytes, std::uint64_t addition) const;
    bool should_cancel() const noexcept;
    void check(int result, const char* operation) const;
    void control(const char* sql);

    Options options_;
    sqlite3* database_{};
    const std::atomic<bool>* stopping_{};
    std::chrono::steady_clock::time_point deadline_{};
    std::chrono::steady_clock::time_point busy_started_{};
    bool internal_control_{};
    bool poisoned_{};
};

} // namespace serverengine::data::sql
