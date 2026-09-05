#include <ServerEngine/C/Sql.h>

#include "Boundary.h"
#include "Data/Sql/Service.h"

#include <cstring>

namespace {
namespace sql = serverengine::data::sql;

template<class Function>
se_status boundary(se_error* error, Function&& function) noexcept
{
    return serverengine::abi::protect(error, [&]() -> se_status {
        try { return function(); }
        catch (const sql::SqlError& failure) {
            return serverengine::abi::fail(error, failure.status, failure.message);
        }
    });
}
} // namespace

extern "C" {

uint32_t SE_CALL se_sql_get_abi_version(void) { return SE_SQL_ABI_VERSION; }

void SE_CALL se_sql_options_init(se_sql_options* options)
{
    if (!options) return;
    *options = {};
    options->struct_size = sizeof(*options);
    options->abi_version = SE_SQL_ABI_VERSION;
    options->provider = SE_SQL_SQLITE;
    options->max_outstanding_requests = 128;
    options->max_request_bytes = 64 * 1024;
    options->max_result_bytes = 1024 * 1024;
    options->max_rows = 10000;
    options->max_columns = 128;
    options->query_timeout_ms = 5000;
    options->busy_timeout_ms = 1000;
    options->memory_budget_bytes = 64ULL * 1024 * 1024;
}

void SE_CALL se_sql_result_init(se_sql_result* result)
{
    if (!result) return;
    *result = {};
    result->struct_size = sizeof(*result);
    result->abi_version = SE_SQL_ABI_VERSION;
}

se_status SE_CALL se_sql_create(const se_sql_options* options, se_sql_handle* service, se_error* error)
{
    if (service) *service = 0;
    return boundary(error, [&]() -> se_status {
        if (!options || !service) throw sql::SqlError(SE_INVALID_ARGUMENT, "SQL options/output handle is null");
        *service = sql::register_service(std::make_shared<sql::Service>(sql::decode_options(*options)));
        return SE_OK;
    });
}

se_status SE_CALL se_sql_submit(se_sql_handle service, const se_sql_statement* statements,
    uint32_t count, uint64_t* request_id, se_error* error)
{
    if (request_id) *request_id = 0;
    return boundary(error, [&]() -> se_status {
        if (!request_id) throw sql::SqlError(SE_INVALID_ARGUMENT, "SQL request ID output is null");
        *request_id = sql::find_service(service)->submit(statements, count);
        return SE_OK;
    });
}

se_status SE_CALL se_sql_poll(se_sql_handle service, se_sql_result* result, uint32_t timeout, se_error* error)
{
    return boundary(error, [&]() -> se_status {
        if (!result || result->struct_size != sizeof(*result) || result->abi_version != SE_SQL_ABI_VERSION)
            throw sql::SqlError(SE_INVALID_ARGUMENT, "SQL result layout/version is invalid");
        return sql::find_service(service)->poll(*result, timeout);
    });
}

se_status SE_CALL se_sql_column_name(se_sql_handle service, uint64_t request_id, uint32_t column,
    char* name, uint32_t capacity, uint32_t* name_size, se_error* error)
{
    if (name_size) *name_size = 0;
    return boundary(error, [&]() -> se_status {
        if (!name_size || (capacity && !name)) throw sql::SqlError(SE_INVALID_ARGUMENT, "invalid SQL column name output");
        return sql::find_service(service)->read_result(request_id, [&](const sql::Result& result) -> se_status {
            if (column >= result.columns.size()) throw sql::SqlError(SE_INVALID_ARGUMENT, "SQL column index out of range");
            const auto& text = result.columns[column];
            *name_size = static_cast<uint32_t>(text.size());
            if (capacity <= text.size()) return SE_BUFFER_TOO_SMALL;
            std::memcpy(name, text.c_str(), text.size() + 1);
            return SE_OK;
        });
    });
}

se_status SE_CALL se_sql_get_cell(se_sql_handle service, uint64_t request_id, uint32_t row,
    uint32_t column, se_sql_cell* cell, void* data, uint32_t capacity, se_error* error)
{
    if (cell) *cell = {};
    return boundary(error, [&]() -> se_status {
        if (!cell || (capacity && !data)) throw sql::SqlError(SE_INVALID_ARGUMENT, "invalid SQL cell output");
        return sql::find_service(service)->read_result(request_id, [&](const sql::Result& result) -> se_status {
            if (row >= result.rows.size() || column >= result.columns.size())
                throw sql::SqlError(SE_INVALID_ARGUMENT, "SQL cell index out of range");
            const auto& value = result.rows[row][column];
            cell->type = value.type;
            cell->integer = value.integer;
            cell->real = value.real;
            cell->size = static_cast<uint32_t>(value.bytes.size());
            if (capacity < value.bytes.size()) return SE_BUFFER_TOO_SMALL;
            if (!value.bytes.empty()) std::memcpy(data, value.bytes.data(), value.bytes.size());
            return SE_OK;
        });
    });
}

se_status SE_CALL se_sql_release_result(se_sql_handle service, uint64_t request_id, se_error* error)
{
    return boundary(error, [&]() -> se_status {
        sql::find_service(service)->release(request_id);
        return SE_OK;
    });
}

se_status SE_CALL se_sql_stop(se_sql_handle service, se_error* error)
{
    return boundary(error, [&]() -> se_status { sql::find_service(service)->stop(); return SE_OK; });
}

se_status SE_CALL se_sql_destroy(se_sql_handle service, se_error* error)
{
    return boundary(error, [&]() -> se_status { sql::remove_service(service)->stop(); return SE_OK; });
}

} // extern "C"
