#include "Types.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace serverengine::data::sql {

Options decode_options(const se_sql_options& source)
{
    if (source.struct_size != sizeof(source) || source.abi_version != SE_SQL_ABI_VERSION
        || std::any_of(std::begin(source.reserved), std::end(source.reserved), [](auto v) { return v != 0; }))
        throw SqlError(SE_INVALID_ARGUMENT, "invalid SQL options layout/version/reserved fields");
    if (source.provider != SE_SQL_SQLITE)
        throw SqlError(SE_NOT_SUPPORTED, "this SQL module provides SQLite; requested provider is unavailable");
    if (!source.connection || source.connection[0] == '\0')
        throw SqlError(SE_INVALID_ARGUMENT, "SQL connection filename is required");
    std::size_t length = 0;
    while (length <= 32768 && source.connection[length] != '\0') ++length;
    if (length > 32768) throw SqlError(SE_INVALID_ARGUMENT, "SQL connection filename too long");
    if (source.max_outstanding_requests < 1 || source.max_outstanding_requests > 4096
        || source.max_request_bytes < 256 || source.max_request_bytes > 16 * 1024 * 1024
        || source.max_result_bytes < 1024 || source.max_result_bytes > 16 * 1024 * 1024
        || source.max_rows < 1 || source.max_rows > 1000000
        || source.max_columns < 1 || source.max_columns > 1024
        || source.query_timeout_ms < 1 || source.query_timeout_ms > 300000
        || source.busy_timeout_ms > 30000
        || source.memory_budget_bytes < static_cast<std::uint64_t>(source.max_result_bytes) + source.max_request_bytes + 1024
        || source.memory_budget_bytes > 1024ULL * 1024 * 1024)
        throw SqlError(SE_INVALID_ARGUMENT, "SQL limits outside supported bounds");
    return {std::string(source.connection, length), source.max_outstanding_requests,
        source.max_request_bytes, source.max_result_bytes, source.max_rows,
        source.max_columns, source.query_timeout_ms, source.busy_timeout_ms, source.memory_budget_bytes};
}

Request copy_request(const se_sql_statement* source, std::uint32_t count, const Options& options)
{
    if (!source || count < 1 || count > 64)
        throw SqlError(SE_INVALID_ARGUMENT, "SQL submit requires 1..64 statements");
    Request request;
    std::uint64_t bytes = sizeof(Request) + count * sizeof(Statement);
    const auto add_bytes = [&](std::uint64_t size) {
        bytes += size;
        if (bytes > options.max_request_bytes)
            throw SqlError(SE_INVALID_ARGUMENT, "SQL request exceeds configured byte limit");
    };
    add_bytes(0);
    request.statements.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& input = source[i];
        if (!input.sql || input.sql_size == 0 || input.sql_size > options.max_request_bytes
            || input.parameter_count > 1024 || (input.parameter_count && !input.parameters))
            throw SqlError(SE_INVALID_ARGUMENT, "invalid SQL statement or parameter array");
        add_bytes(input.sql_size + static_cast<std::uint64_t>(input.parameter_count) * sizeof(Value));
        if (std::memchr(input.sql, 0, input.sql_size))
            throw SqlError(SE_INVALID_ARGUMENT, "SQL text must not contain embedded NUL bytes");
        Statement statement;
        statement.sql.assign(input.sql, input.sql_size);
        statement.parameters.reserve(input.parameter_count);
        for (std::uint32_t p = 0; p < input.parameter_count; ++p) {
            const auto& parameter = input.parameters[p];
            if (parameter.type > SE_SQL_BLOB
                || (parameter.type == SE_SQL_DOUBLE && !std::isfinite(parameter.real))
                || (parameter.type < SE_SQL_TEXT && (parameter.size || parameter.data))
                || (parameter.type >= SE_SQL_TEXT && parameter.size && !parameter.data))
                throw SqlError(SE_INVALID_ARGUMENT, "invalid SQL parameter type, pointer or numeric value");
            add_bytes(parameter.size);
            Value value;
            value.type = parameter.type;
            value.integer = parameter.integer;
            value.real = parameter.real;
            if (parameter.size) value.bytes.assign(static_cast<const char*>(parameter.data), parameter.size);
            statement.parameters.push_back(std::move(value));
        }
        request.statements.push_back(std::move(statement));
    }
    // Reserve the maximum completion size before accepting a job. Writes never
    // lose their outcome because a later poll queue happens to be full.
    request.reserved_bytes = bytes + options.max_result_bytes + 1024;
    return request;
}

} // namespace serverengine::data::sql
