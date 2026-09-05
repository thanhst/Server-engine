#include "SqliteDriver.h"

#include <memory>

namespace serverengine::data::sql {
namespace {
using Prepared = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

int bind(sqlite3_stmt* statement, int index, const Value& value)
{
    switch (value.type) {
    case SE_SQL_NULL: return sqlite3_bind_null(statement, index);
    case SE_SQL_INT64: return sqlite3_bind_int64(statement, index, value.integer);
    case SE_SQL_DOUBLE: return sqlite3_bind_double(statement, index, value.real);
    case SE_SQL_TEXT:
        return sqlite3_bind_text(statement, index, value.bytes.data(), static_cast<int>(value.bytes.size()), SQLITE_TRANSIENT);
    case SE_SQL_BLOB:
        // data() is non-null even for empty std::string, preserving empty BLOB vs NULL.
        return sqlite3_bind_blob(statement, index, value.bytes.data(), static_cast<int>(value.bytes.size()), SQLITE_TRANSIENT);
    default: return SQLITE_MISUSE;
    }
}
} // namespace

void SqliteDriver::execute_statement(const Statement& input, bool final, Result& result, std::uint64_t& bytes)
{
    sqlite3_stmt* raw = nullptr;
    const char* tail = nullptr;
    const int prepared = sqlite3_prepare_v2(database_, input.sql.c_str(),
        static_cast<int>(input.sql.size()), &raw, &tail);
    Prepared statement(raw, sqlite3_finalize);
    check(prepared, "SQLite prepare failed; verify SQL syntax and permitted operations");
    if (!statement) throw SqlError(SE_INVALID_ARGUMENT, "empty SQL statement");
    // Preparing the tail recognizes comments correctly but never executes it.
    sqlite3_stmt* extra_raw = nullptr;
    const auto remaining = input.sql.c_str() + input.sql.size() - tail;
    const int extra_code = sqlite3_prepare_v2(database_, tail, static_cast<int>(remaining), &extra_raw, nullptr);
    Prepared extra(extra_raw, sqlite3_finalize);
    check(extra_code, "invalid trailing SQL text");
    if (extra) throw SqlError(SE_INVALID_ARGUMENT, "use a statement array for atomic batches; SQL text contains multiple statements");
    if (sqlite3_bind_parameter_count(statement.get()) != static_cast<int>(input.parameters.size()))
        throw SqlError(SE_INVALID_ARGUMENT, "SQL parameter count does not match placeholders");
    for (std::size_t i = 0; i < input.parameters.size(); ++i)
        check(bind(statement.get(), static_cast<int>(i + 1), input.parameters[i]), "SQLite parameter binding failed");
    if (!final && sqlite3_column_count(statement.get()) != 0)
        throw SqlError(SE_INVALID_ARGUMENT, "only the final batch statement may return columns");
    if (final) read_columns(statement.get(), result, bytes);
    const auto changes_before = sqlite3_total_changes64(database_);
    int step = SQLITE_OK;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        if (should_cancel()) check(SQLITE_INTERRUPT, "SQL request cancelled");
        read_row(statement.get(), result, bytes);
    }
    check(step, "SQLite statement execution failed");
    if (sqlite3_total_changes64(database_) != changes_before)
        result.affected_rows += static_cast<std::uint64_t>(sqlite3_changes64(database_));
}

void SqliteDriver::account(std::uint64_t& bytes, std::uint64_t addition) const
{
    if (addition > options_.max_result_bytes || bytes > options_.max_result_bytes - addition)
        throw SqlError(SE_RESULT_TOO_LARGE, "SQL result exceeds configured row/column/byte limit; transaction rolled back");
    bytes += addition;
}

void SqliteDriver::read_columns(sqlite3_stmt* statement, Result& result, std::uint64_t& bytes)
{
    const int count = sqlite3_column_count(statement);
    if (count > static_cast<int>(options_.max_columns))
        throw SqlError(SE_RESULT_TOO_LARGE, "SQL result exceeds configured column limit");
    account(bytes, static_cast<std::uint64_t>(count) * sizeof(std::string));
    result.columns.reserve(count);
    for (int i = 0; i < count; ++i) {
        const char* name = sqlite3_column_name(statement, i);
        if (!name) throw SqlError(SE_INTERNAL_ERROR, "SQLite column name allocation failed");
        std::string column(name);
        account(bytes, column.size() * 2 + 1);
        result.columns.push_back(std::move(column));
    }
}

void SqliteDriver::read_row(sqlite3_stmt* statement, Result& result, std::uint64_t& bytes)
{
    if (result.rows.size() >= options_.max_rows)
        throw SqlError(SE_RESULT_TOO_LARGE, "SQL result exceeds configured row limit; transaction rolled back");
    account(bytes, 2 * sizeof(std::vector<Value>) + result.columns.size() * sizeof(Value));
    std::vector<Value> row;
    row.reserve(result.columns.size());
    for (int i = 0; i < static_cast<int>(result.columns.size()); ++i) {
        Value value;
        switch (sqlite3_column_type(statement, i)) {
        case SQLITE_NULL: break;
        case SQLITE_INTEGER:
            value.type = SE_SQL_INT64;
            value.integer = sqlite3_column_int64(statement, i);
            break;
        case SQLITE_FLOAT:
            value.type = SE_SQL_DOUBLE;
            value.real = sqlite3_column_double(statement, i);
            break;
        case SQLITE_TEXT:
        case SQLITE_BLOB: {
            const bool text = sqlite3_column_type(statement, i) == SQLITE_TEXT;
            value.type = text ? SE_SQL_TEXT : SE_SQL_BLOB;
            const void* data = text ? static_cast<const void*>(sqlite3_column_text(statement, i))
                                    : sqlite3_column_blob(statement, i);
            const int size = sqlite3_column_bytes(statement, i);
            if ((!data && size != 0) || sqlite3_errcode(database_) == SQLITE_NOMEM)
                throw SqlError(SE_INTERNAL_ERROR, "SQLite cell allocation failed");
            account(bytes, static_cast<std::uint64_t>(size) * 2 + 1);
            if (size) value.bytes.assign(static_cast<const char*>(data), size);
            break;
        }
        default: throw SqlError(SE_INTERNAL_ERROR, "unknown SQLite value type");
        }
        row.push_back(std::move(value));
    }
    result.rows.push_back(std::move(row));
}

} // namespace serverengine::data::sql
