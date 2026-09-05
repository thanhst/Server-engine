#include "SqliteDriver.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <thread>

namespace serverengine::data::sql {

std::unique_ptr<Driver> make_sqlite_driver(const Options& options)
{
    return std::make_unique<SqliteDriver>(options);
}

SqliteDriver::SqliteDriver(const Options& options) : options_(options)
{
    const int opened = sqlite3_open_v2(options.connection.c_str(), &database_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    try {
        check(opened, "cannot open SQLite database");
        check(sqlite3_db_config(database_, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, nullptr),
            "cannot disable SQLite extension loading");
        check(sqlite3_db_config(database_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr),
            "cannot enable SQLite defensive mode");
        check(sqlite3_db_config(database_, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr),
            "cannot disable SQLite trusted schema");
        sqlite3_limit(database_, SQLITE_LIMIT_LENGTH, static_cast<int>(options.max_result_bytes));
        sqlite3_limit(database_, SQLITE_LIMIT_SQL_LENGTH, static_cast<int>(options.max_request_bytes));
        sqlite3_limit(database_, SQLITE_LIMIT_COLUMN, static_cast<int>(options.max_columns));
        sqlite3_limit(database_, SQLITE_LIMIT_VARIABLE_NUMBER, 1024);
        sqlite3_limit(database_, SQLITE_LIMIT_ATTACHED, 0);
        check(sqlite3_set_authorizer(database_, authorize, this), "cannot configure SQLite authorizer");
        check(sqlite3_busy_handler(database_, busy, this), "cannot configure SQLite busy handler");
    } catch (...) {
        if (database_) sqlite3_close_v2(database_);
        database_ = nullptr;
        throw;
    }
}

SqliteDriver::~SqliteDriver()
{
    if (database_) sqlite3_close_v2(database_);
}

int SqliteDriver::authorize(void* context, int action, const char*, const char* second,
    const char*, const char*) noexcept
{
    auto& self = *static_cast<SqliteDriver*>(context);
    switch (action) {
    case SQLITE_TRANSACTION:
    case SQLITE_SAVEPOINT:
        return self.internal_control_ ? SQLITE_OK : SQLITE_DENY;
    case SQLITE_ATTACH:
    case SQLITE_DETACH:
    case SQLITE_PRAGMA:
    case SQLITE_CREATE_VTABLE:
    case SQLITE_DROP_VTABLE:
        return SQLITE_DENY;
    case SQLITE_FUNCTION:
        return second && sqlite3_stricmp(second, "load_extension") == 0 ? SQLITE_DENY : SQLITE_OK;
    default:
        return SQLITE_OK;
    }
}

bool SqliteDriver::should_cancel() const noexcept
{
    return stopping_ && (stopping_->load(std::memory_order_relaxed)
        || std::chrono::steady_clock::now() >= deadline_);
}

int SqliteDriver::progress(void* context) noexcept
{
    return static_cast<SqliteDriver*>(context)->should_cancel() ? 1 : 0;
}

int SqliteDriver::busy(void* context, int attempts) noexcept
{
    auto& self = *static_cast<SqliteDriver*>(context);
    if (attempts == 0) self.busy_started_ = std::chrono::steady_clock::now();
    if (self.should_cancel() || std::chrono::steady_clock::now() - self.busy_started_
        >= std::chrono::milliseconds(self.options_.busy_timeout_ms)) return 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return 1;
}

void SqliteDriver::check(int result, const char* operation) const
{
    if (result == SQLITE_OK || result == SQLITE_DONE) return;
    if (should_cancel())
        throw SqlError(stopping_->load(std::memory_order_relaxed) ? SE_SQL_CANCELLED : SE_TIMEOUT,
            "SQL request cancelled or execution deadline exceeded; transaction rolled back");
    if (result == SQLITE_TOOBIG)
        throw SqlError(SE_RESULT_TOO_LARGE, "SQLite value exceeds configured size limit");
    throw SqlError(SE_IO_ERROR, operation);
}

void SqliteDriver::control(const char* sql)
{
    internal_control_ = true;
    const int result = sqlite3_exec(database_, sql, nullptr, nullptr, nullptr);
    internal_control_ = false;
    // Once SQLite has left transaction mode after a failed COMMIT we cannot
    // promise the caller that a write was cancelled. Conservatively expose an
    // ambiguous outcome; applications must reconcile instead of retrying blindly.
    if (result != SQLITE_OK && std::strcmp(sql, "COMMIT") == 0 && sqlite3_get_autocommit(database_) != 0)
        throw SqlError(SE_OUTCOME_UNKNOWN, "SQLite COMMIT failed after transaction ended; reconcile write outcome");
    check(result, "SQLite transaction control failed");
}

void SqliteDriver::interrupt() noexcept
{
    sqlite3_interrupt(database_);
}

void SqliteDriver::execute(Request& request, const std::atomic<bool>& stopping) noexcept
{
    auto& result = request.result;
    if (poisoned_) {
        result.fail(SE_INVALID_STATE, "SQLite connection unavailable after rollback failure; recreate service");
        return;
    }
    stopping_ = &stopping;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(options_.query_timeout_ms);
    sqlite3_progress_handler(database_, 1000, progress, this);
    try {
        if (should_cancel()) throw SqlError(SE_SQL_CANCELLED, "SQL request cancelled before execution");
        control("BEGIN"); // Deferred transaction avoids a write lock for SELECT-only jobs.
        std::uint64_t bytes = sizeof(Result);
        for (std::size_t i = 0; i < request.statements.size(); ++i) {
            if (should_cancel()) check(SQLITE_INTERRUPT, "SQL request cancelled");
            execute_statement(request.statements[i], i + 1 == request.statements.size(), result, bytes);
        }
        if (should_cancel()) check(SQLITE_INTERRUPT, "SQL request cancelled");
        control("COMMIT");
        // Do not recheck cancellation here: a committed write is successful even
        // when stop races with completion publication.
        result.last_insert_id = sqlite3_last_insert_rowid(database_);
    } catch (const SqlError& error) {
        result.fail(error.status, error.message);
    } catch (const std::bad_alloc&) {
        result.fail(SE_INTERNAL_ERROR, "SQL result allocation failed; transaction rolled back");
    } catch (...) {
        result.fail(SE_INTERNAL_ERROR, "SQL execution failed; transaction rolled back");
    }
    sqlite3_progress_handler(database_, 0, nullptr, nullptr);
    stopping_ = nullptr; // Rollback must not inherit the cancelled progress callback.
    if (sqlite3_get_autocommit(database_) == 0) {
        internal_control_ = true;
        const int rolled_back = sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        internal_control_ = false;
        if (rolled_back != SQLITE_OK || sqlite3_get_autocommit(database_) == 0) {
            poisoned_ = true;
            result.fail(SE_OUTCOME_UNKNOWN, "SQLite rollback failed; discard service and reconcile outcome");
        }
    }
}

} // namespace serverengine::data::sql
