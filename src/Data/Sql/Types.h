#pragma once

#include <ServerEngine/C/Sql.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace serverengine::data::sql {

struct Options {
    std::string connection;
    std::uint32_t max_outstanding{}, max_request_bytes{}, max_result_bytes{};
    std::uint32_t max_rows{}, max_columns{}, query_timeout_ms{}, busy_timeout_ms{};
    std::uint64_t memory_budget{};
};

struct Value {
    std::uint32_t type{SE_SQL_NULL};
    std::int64_t integer{};
    double real{};
    std::string bytes; // Binary-safe storage for TEXT and BLOB.
};

struct Statement {
    std::string sql;
    std::vector<Value> parameters;
};

struct Result {
    se_status status{SE_OK};
    std::array<char, 256> message{};
    std::uint64_t affected_rows{};
    std::int64_t last_insert_id{};
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    void fail(se_status code, const char* diagnostic) noexcept;
};

struct Request {
    enum class State { Queued, Running, Complete, Delivered };
    std::uint64_t id{};
    std::uint64_t reserved_bytes{};
    std::vector<Statement> statements;
    Result result;
    State state{State::Queued};
};

class SqlError {
public:
    SqlError(se_status status, const char* message) : status(status), message(message) {}
    se_status status;
    const char* message;
};

Options decode_options(const se_sql_options& source);
Request copy_request(const se_sql_statement* source, std::uint32_t count, const Options& options);

} // namespace serverengine::data::sql
