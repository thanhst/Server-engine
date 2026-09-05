#ifndef SERVERENGINE_C_REDIS_H
#define SERVERENGINE_C_REDIS_H

#include <ServerEngine/C/ServerEngine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional NoSQL connector. Check se_get_abi_version() before initializers.
 * This API is always exported; open returns SE_NOT_SUPPORTED when Redis was
 * omitted from the build. One handle owns one worker and one connection.
 * No generic command API: connection state cannot leak between requests. */
typedef uint64_t se_redis_handle;
#define SE_REDIS_GET UINT32_C(1)
#define SE_REDIS_SET UINT32_C(2)
#define SE_REDIS_DELETE UINT32_C(3)

#pragma pack(push, 8)
typedef struct se_redis_options {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* address; /* Numeric IPv4/IPv6; NULL = 127.0.0.1. Copied. */
    const char* username; /* Optional Redis ACL user; requires password. */
    const char* password; /* Optional UTF-8 credential, copied. Never logged. */
    uint32_t port;
    uint32_t security; /* Explicit SE_SECURITY_NONE required. TLS unsupported. */
    uint32_t connect_timeout_ms;
    uint32_t command_timeout_ms; /* Entire network operation, excluding queue wait. */
    uint32_t max_inflight_requests; /* Queued + running + completed, until polled. */
    uint32_t max_key_bytes;
    uint32_t max_value_bytes;
    uint32_t reserved32;
    uint64_t max_queue_bytes; /* Copied keys and values, including the running job. */
    uint64_t max_result_bytes; /* GET reserves max_value_bytes until result consumed. */
    uint64_t reserved[4];
} se_redis_options;

typedef struct se_redis_result {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t request_id;
    uint32_t operation;
    se_status status; /* Operation outcome, independent of poll's return status. */
    uint32_t found; /* GET: distinguishes a missing key from an empty value. */
    uint32_t value_size; /* GET binary payload, never a string terminator. */
    uint64_t affected_count; /* SET: 1; DELETE: 0 or 1. */
    char message[192]; /* Sanitized, NUL-terminated error, no credentials/key/value. */
    uint64_t reserved[4];
} se_redis_result;
#pragma pack(pop)

SE_API void SE_CALL se_redis_options_init(se_redis_options* options);
SE_API void SE_CALL se_redis_result_init(se_redis_result* result);
/* open creates the worker; connection/authentication happen on its first job.
 * Success does not establish that the endpoint is reachable. Database 0 only.
 * Default security is TLS, so callers must explicitly choose plaintext NONE.
 * NONE sends credentials and data unencrypted: use only a trusted local/private
 * connection or a separately configured secure tunnel. Network TLS is unrelated. */
SE_API se_status SE_CALL se_redis_open(const se_redis_options* options,
    se_redis_handle* redis, se_error* error);
/* Accepted input is copied before returning. Request IDs increase per handle.
 * BACKPRESSURE means nothing was submitted: poll completions, then retry.
 * Keys must contain 1..max_key_bytes; values may be empty and contain NUL bytes. */
SE_API se_status SE_CALL se_redis_get(se_redis_handle redis,
    const void* key, uint32_t key_size, uint64_t* request_id, se_error* error);
/* ttl_ms=0 means no expiry. A SET replaces the existing value and its old TTL.
 * No automatic write retry. OUTCOME_UNKNOWN means Redis may have applied the
 * write before a disconnect/timeout; reconcile instead of blindly retrying. */
SE_API se_status SE_CALL se_redis_set(se_redis_handle redis,
    const void* key, uint32_t key_size, const void* value, uint32_t value_size,
    uint64_t ttl_ms, uint64_t* request_id, se_error* error);
SE_API se_status SE_CALL se_redis_delete(se_redis_handle redis,
    const void* key, uint32_t key_size, uint64_t* request_id, se_error* error);
/* SE_OK means one completion consumed; inspect result.status for its outcome.
 * BUFFER_TOO_SMALL fills metadata and keeps the completion for a retry.
 * timeout_ms=0 is nonblocking. One consumer per handle is recommended. */
SE_API se_status SE_CALL se_redis_poll(se_redis_handle redis,
    se_redis_result* result, void* value, uint32_t capacity,
    uint32_t timeout_ms, se_error* error);
/* Terminal and idempotent. Cancels connection I/O, joins the worker, and wakes
 * pollers. Accepted jobs still have completions: queued jobs get SE_STOPPED;
 * an interrupted write may get SE_OUTCOME_UNKNOWN. Drain before destroy. */
SE_API se_status SE_CALL se_redis_stop(se_redis_handle redis, se_error* error);
/* Invalidates handle; stops worker and releases unconsumed results. */
SE_API se_status SE_CALL se_redis_destroy(se_redis_handle redis, se_error* error);

#ifdef __cplusplus
}
#endif
#endif
