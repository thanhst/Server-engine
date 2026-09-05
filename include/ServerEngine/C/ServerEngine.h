#ifndef SERVERENGINE_C_API_H
#define SERVERENGINE_C_API_H

/* Public C ABI. UTF-8 strings, fixed-width numbers, caller-owned buffers.
 * No STL objects, exceptions, sockets or allocator ownership cross this boundary.
 * Build the host and DLL for the same architecture. Never unload an active DLL.
 * Check se_get_abi_version() BEFORE calling initializers. V1 struct layouts are
 * frozen; a future incompatible layout requires new versioned exports. */
#include <stdint.h>

#if defined(_WIN32)
#define SE_CALL __cdecl
#if defined(SERVERENGINE_DLL_BUILD)
#define SE_API __declspec(dllexport)
#else
#define SE_API __declspec(dllimport)
#endif
#else
#define SE_CALL
#define SE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SE_ABI_VERSION UINT32_C(0x00010000)
typedef uint64_t se_server_handle;
typedef int32_t se_status;

#define SE_OK ((se_status)0)
#define SE_TIMEOUT ((se_status)1)
#define SE_BUFFER_TOO_SMALL ((se_status)2)
#define SE_STOPPED ((se_status)3)
#define SE_INVALID_ARGUMENT ((se_status)-1)
#define SE_INVALID_HANDLE ((se_status)-2)
#define SE_INVALID_STATE ((se_status)-3)
#define SE_IO_ERROR ((se_status)-4)
#define SE_BACKPRESSURE ((se_status)-5)
#define SE_INTERNAL_ERROR ((se_status)-6)
#define SE_NOT_SUPPORTED ((se_status)-7)
#define SE_RESULT_TOO_LARGE ((se_status)-8)
#define SE_OUTCOME_UNKNOWN ((se_status)-9)

#define SE_PROTOCOL_TCP UINT32_C(1)
#define SE_PROTOCOL_UDP UINT32_C(2)
#define SE_PROTOCOL_WEBSOCKET UINT32_C(3)
#define SE_PROTOCOL_HTTP UINT32_C(4)
#define SE_SECURITY_NONE UINT32_C(0)
#define SE_SECURITY_TLS UINT32_C(1)
#define SE_EVENT_OPEN UINT32_C(1)
#define SE_EVENT_MESSAGE UINT32_C(2)
#define SE_EVENT_CLOSE UINT32_C(3)
#define SE_EVENT_ERROR UINT32_C(4)
#define SE_EVENT_OVERFLOW UINT32_C(5)
#define SE_EVENT_HTTP_REQUEST UINT32_C(6) /* Decode with ServerEngine/C/Http.h. */

#pragma pack(push, 8)
typedef struct se_error {
    int32_t code;
    char message[252]; /* Always terminated. Optional output; may be NULL. */
} se_error;

typedef struct se_server_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t max_connections;
    uint32_t max_message_bytes;
    uint32_t max_send_queue_bytes; /* Per peer; >= max_message_bytes + 16. */
    uint32_t max_event_queue_count;
    uint64_t max_event_queue_bytes;
    uint64_t idle_timeout_ms;
    uint64_t reserved[4]; /* Must be zero. */
} se_server_options;

typedef struct se_listener_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t protocol;
    uint32_t security;
    uint32_t port; /* 1..65535. */
    uint32_t handshake_timeout_ms;
    const char* bind_address; /* Numeric IPv4/IPv6; NULL = 127.0.0.1. Copied on add. */
    const char* certificate_chain_file;
    const char* private_key_file;
    const char* websocket_path; /* NULL = /. Strings must be valid UTF-8 and NUL-terminated. */
    uint64_t reserved[4];
} se_listener_options;

typedef struct se_event {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t kind;
    uint32_t protocol;
    uint64_t session_id;
    uint64_t listener_id;
    uint64_t sequence;
    uint32_t payload_size;
    uint32_t peer_port;
    char peer_address[64];
} se_event;
#pragma pack(pop)

SE_API uint32_t SE_CALL se_get_abi_version(void);
SE_API void SE_CALL se_server_options_init(se_server_options* options);
SE_API void SE_CALL se_listener_options_init(se_listener_options* options);
SE_API void SE_CALL se_event_init(se_event* event);
SE_API se_status SE_CALL se_server_create(const se_server_options* options,
    se_server_handle* server, se_error* error);
/* Add listeners before start. A start failure leaves the configuration editable. */
SE_API se_status SE_CALL se_server_add_listener(se_server_handle server,
    const se_listener_options* options, uint64_t* listener_id, se_error* error);
SE_API se_status SE_CALL se_server_start(se_server_handle server, se_error* error);
/* Stop is terminal and idempotent. Create a new handle to restart. */
SE_API se_status SE_CALL se_server_stop(se_server_handle server, se_error* error);
/* Invalidates the handle, joins I/O, wakes pollers. Never frees host memory. */
SE_API se_status SE_CALL se_server_destroy(se_server_handle server, se_error* error);
/* TCP: DLL adds a 4-byte big-endian length. WS: one binary message. UDP: one
 * datagram. Success means queued locally, not remote delivery or acknowledgement. */
SE_API se_status SE_CALL se_server_send(se_server_handle server, uint64_t session_id,
    const void* data, uint32_t size, se_error* error);
SE_API se_status SE_CALL se_server_disconnect(se_server_handle server,
    uint64_t session_id, se_error* error);
/* One consumer per handle is recommended. timeout_ms=0 means nonblocking.
 * MESSAGE payload is binary; ERROR payload is UTF-8 without a terminator.
 * BUFFER_TOO_SMALL fills event metadata but leaves the event queued for retry.
 * OVERFLOW means events were lost: this poll stops/joins the server before
 * returning. Until polled, further events/sends are refused; resources remain
 * bounded and allocated until poll, stop or destroy. Recreate after overflow.
 * OPEN/CLOSE for UDP describe a logical remote endpoint, not authentication. */
SE_API se_status SE_CALL se_server_poll_event(se_server_handle server, se_event* event,
    void* payload, uint32_t capacity, uint32_t timeout_ms, se_error* error);

#ifdef __cplusplus
}
#endif
#endif
