#ifndef SERVERENGINE_C_SQL_H
#define SERVERENGINE_C_SQL_H

#include <ServerEngine/C/ServerEngine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Independent Data/SQL service. No network server handle is required.
 * Check se_sql_get_abi_version() before using the frozen V1 layouts. */
#define SE_SQL_ABI_VERSION UINT32_C(0x00010000)
#define SE_SQL_SQLITE UINT32_C(1)
#define SE_SQL_MYSQL UINT32_C(2) /* Reserved provider; unavailable in this release. */
#define SE_SQL_NULL UINT32_C(0)
#define SE_SQL_INT64 UINT32_C(1)
#define SE_SQL_DOUBLE UINT32_C(2)
#define SE_SQL_TEXT UINT32_C(3)
#define SE_SQL_BLOB UINT32_C(4)
#define SE_SQL_CANCELLED ((se_status)-100)

typedef uint64_t se_sql_handle;

#pragma pack(push, 8)
typedef struct se_sql_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t provider;
    uint32_t max_outstanding_requests;
    uint32_t max_request_bytes; /* Includes copied statement/value metadata. */
    uint32_t max_result_bytes; /* Engine result accounting, including row/cell overhead. */
    uint32_t max_rows;
    uint32_t max_columns; /* Also restricts SQLite schema/query column complexity. */
    uint32_t query_timeout_ms; /* Cooperative execution deadline, excludes queue wait. */
    uint32_t busy_timeout_ms;
    uint64_t memory_budget_bytes; /* Engine reservations; excludes SQLite cache/VM and allocator overhead. */
    const char* connection; /* SQLite UTF-8 filename or :memory:, copied by create. */
    uint64_t reserved[4];
} se_sql_options;

/* Input strings/blobs need not be terminated. SQL and parameter data are copied
 * before submit returns. TEXT is UTF-8; embedded NUL bytes remain valid values.
 * Use zero-initialized parameters; size/data apply only to TEXT/BLOB. */
typedef struct se_sql_parameter {
    uint32_t type;
    uint32_t size;
    int64_t integer;
    double real;
    const void* data;
} se_sql_parameter;

typedef struct se_sql_statement {
    const char* sql;
    uint32_t sql_size; /* Explicit byte length, excluding NUL. Exactly one statement. */
    uint32_t parameter_count;
    const se_sql_parameter* parameters; /* Positional parameters, in binding order. */
} se_sql_statement;

typedef struct se_sql_result {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t request_id; /* Also the result handle, scoped to this SQL service. */
    int32_t status; /* Execution outcome; poll itself returns SE_OK for a failed query. */
    uint32_t row_count;
    uint32_t column_count;
    uint32_t reserved;
    uint64_t affected_rows; /* Sum of SQLite direct DML changes, excludes triggers. */
    int64_t last_insert_id; /* Connection's SQLite last_insert_rowid after success. */
    char message[256]; /* Terminated diagnostic, never SQL or bound parameter values. */
} se_sql_result;

typedef struct se_sql_cell {
    uint32_t type;
    uint32_t size; /* TEXT/BLOB byte count, excluding any terminator. */
    int64_t integer;
    double real;
} se_sql_cell;
#pragma pack(pop)

SE_API uint32_t SE_CALL se_sql_get_abi_version(void);
SE_API void SE_CALL se_sql_options_init(se_sql_options* options);
SE_API void SE_CALL se_sql_result_init(se_sql_result* result);
/* Create opens the database synchronously. Queries run on a dedicated worker. */
SE_API se_status SE_CALL se_sql_create(const se_sql_options* options,
    se_sql_handle* service, se_error* error);
/* 1..64 statements form ONE atomic transaction. Only the final statement may
 * return columns. BEGIN/COMMIT/SAVEPOINT, ATTACH, PRAGMA and extension loading
 * are rejected: transaction ownership cannot leak between requests.
 * Accepted requests reserve completion capacity until release_result: callers
 * must poll AND release, even for failed writes. Full capacity returns
 * SE_BACKPRESSURE without executing anything. Request IDs are never reused. */
SE_API se_status SE_CALL se_sql_submit(se_sql_handle service,
    const se_sql_statement* statements, uint32_t statement_count,
    uint64_t* request_id, se_error* error);
/* One consumer recommended. Poll removes one completion from the queue and
 * retains its result until release_result or destroy. Empty stopped => STOPPED. */
SE_API se_status SE_CALL se_sql_poll(se_sql_handle service, se_sql_result* result,
    uint32_t timeout_ms, se_error* error);
/* Name requires capacity >= name_size + 1 and is NUL-terminated on success.
 * Too-small getters report sizes and retain the result for a safe retry. */
SE_API se_status SE_CALL se_sql_column_name(se_sql_handle service, uint64_t request_id,
    uint32_t column, char* name, uint32_t capacity, uint32_t* name_size, se_error* error);
/* Scalar values are in cell. TEXT/BLOB are copied without a terminator. */
SE_API se_status SE_CALL se_sql_get_cell(se_sql_handle service, uint64_t request_id,
    uint32_t row, uint32_t column, se_sql_cell* cell,
    void* data, uint32_t capacity, se_error* error);
SE_API se_status SE_CALL se_sql_release_result(se_sql_handle service,
    uint64_t request_id, se_error* error);
/* Stop is terminal/idempotent, rejects new submits, cancels queued work and
 * interrupts active work, joins worker, then preserves every accepted outcome
 * for polling. A write that committed before stop remains successful.
 * Destroy stops and invalidates all results. Never unload DLL before destroy. */
SE_API se_status SE_CALL se_sql_stop(se_sql_handle service, se_error* error);
SE_API se_status SE_CALL se_sql_destroy(se_sql_handle service, se_error* error);

#ifdef __cplusplus
}
#endif
#endif
