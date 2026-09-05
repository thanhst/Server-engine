#ifndef SERVERENGINE_C_HTTP_H
#define SERVERENGINE_C_HTTP_H

#include <ServerEngine/C/ServerEngine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Listen with SE_PROTOCOL_HTTP and security NONE (HTTP) or TLS (HTTPS).
 * HTTP/1.1 only, one in-flight request per connection. Poll normal OPEN/CLOSE
 * events and SE_EVENT_HTTP_REQUEST with se_server_poll_event. Raw server_send
 * is not valid for HTTP sessions; use se_http_respond instead.
 * Request headers <=16KiB, target <=2048 bytes, method <=32 bytes. The COMPLETE
 * event payload (metadata + method + target + headers + body) must fit the
 * server's max_message_bytes. Bodies are buffered; this is not a streaming API.
 * handshake_timeout_ms bounds each complete request read; idle_timeout_ms
 * bounds the application's response and network write. Expiry closes session.
 * Expect, CONNECT, Upgrade and HTTP/2 are rejected; use a WebSocket listener
 * for upgrades. Chunked request bodies are decoded; content encodings are not. */

#pragma pack(push, 8)
typedef struct se_http_request {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t request_id; /* Unique within this server lifetime, never reused. */
    uint32_t method_offset;
    uint32_t method_size;
    uint32_t target_offset;
    uint32_t target_size;
    uint32_t headers_offset;
    uint32_t headers_size;
    uint32_t body_offset;
    uint32_t body_size;
} se_http_request;

typedef struct se_http_header {
    const char* name;
    const char* value;
} se_http_header;

typedef struct se_http_response {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t status_code; /* 200..599. Defaults to 200. */
    uint32_t close_connection; /* 0 keeps alive when request permits; 1 closes. */
    const char* content_type; /* NULL defaults to application/octet-stream. */
    const se_http_header* headers; /* Optional extra headers, max 64. */
    uint32_t header_count;
    uint32_t body_size;
    const void* body;
    uint64_t reserved[4];
} se_http_response;
#pragma pack(pop)

/* Copies validated metadata from a HTTP_REQUEST event. No alignment required
 * for payload. Output is a full v1 struct: check DLL ABI before calling.
 * Offsets address the SAME payload bytes; strings have no NUL terminators.
 * Headers are parsed fields serialized as "name: value\r\n" (duplicates kept),
 * including received trailers. Target remains escaped and includes query text;
 * application owns routing/decoding. Decode only HTTP_REQUEST event payloads. */
SE_API se_status SE_CALL se_http_request_read(const void* payload, uint32_t size,
    se_http_request* request, se_error* error);
SE_API void SE_CALL se_http_response_init(se_http_response* response);
/* Copies caller data before returning. Success means locally queued, not sent.
 * A request accepts one response. Stale request/session IDs fail; a timed-out
 * request event may still be queued, so always check this function's result.
 * Response body <= max_message_bytes. Custom header names must be HTTP tokens;
 * values cannot contain CR/LF or controls. Framing/hop-by-hop headers and
 * Content-Type cannot be set through headers; engine owns those fields.
 * Header block <=16KiB; wire response must fit max_send_queue_bytes.
 * For HEAD, pass the representation's body: Content-Length is its size and
 * the DLL omits bytes on the wire. 204/205/304 require an empty body.
 * Content-Length is omitted for 204/304 and zero for 205. */
SE_API se_status SE_CALL se_http_respond(se_server_handle server,
    uint64_t session_id, uint64_t request_id, const se_http_response* response,
    se_error* error);

#ifdef __cplusplus
}
#endif
#endif
