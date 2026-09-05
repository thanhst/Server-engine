#ifndef SERVERENGINE_GAME_TRANSPORT_H
#define SERVERENGINE_GAME_TRANSPORT_H

/* Optional game message transport, shared by native clients and servers.
 * Implemented with Valve GameNetworkingSockets; no vendor types cross this ABI.
 * This is a separate wire protocol from raw SE_PROTOCOL_UDP and TCP framing.
 * Check version before calling initializers. V1 layouts are frozen. */
#include <ServerEngine/C/ServerEngine.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SE_GAME_ABI_VERSION UINT32_C(0x00010000)
#define SE_GAME_UNRELIABLE UINT32_C(0)
#define SE_GAME_RELIABLE_ORDERED UINT32_C(1)
#define SE_GAME_CONNECTED UINT32_C(1)
#define SE_GAME_MESSAGE UINT32_C(2)
#define SE_GAME_DISCONNECTED UINT32_C(3)
#define SE_GAME_OVERFLOW UINT32_C(4)
/* Encryption is mandatory, but direct-IP peers have NO verified identity.
 * Default endpoints allow loopback only. This flag permits remote addresses;
 * use only in a trusted/protected network. It does not authenticate players,
 * pin a server key or prevent active MITM. Do not send secrets over an
 * untrusted Internet path on the strength of encryption alone. */
#define SE_GAME_ALLOW_REMOTE_UNAUTHENTICATED UINT32_C(1)

typedef uint64_t se_game_handle;

#pragma pack(push, 8)
typedef struct se_game_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t max_peers;
    uint32_t max_message_bytes;
    uint32_t max_send_queue_bytes; /* Per connection; includes reliable backlog. */
    uint32_t max_event_queue_count;
    uint32_t connect_timeout_ms;
    uint64_t max_event_queue_bytes;
    uint64_t reserved[4];
} se_game_options;

typedef struct se_game_event {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t kind;
    uint32_t delivery;
    uint64_t peer_id; /* Engine ID, scoped to this endpoint, never a player ID. */
    uint32_t payload_size;
    int32_t reason; /* Transport-specific disconnect code; diagnostic only. */
    char message[128]; /* UTF-8 diagnostic, terminated. Never credentials. */
    uint64_t reserved[4];
} se_game_event;
#pragma pack(pop)

SE_API uint32_t SE_CALL se_game_get_abi_version(void);
SE_API void SE_CALL se_game_options_init(se_game_options* options);
SE_API void SE_CALL se_game_event_init(se_game_event* event);
/* Feature disabled: create returns SE_NOT_SUPPORTED and clears output. */
SE_API se_status SE_CALL se_game_create(const se_game_options* options,
    se_game_handle* endpoint, se_error* error);
/* One listener per endpoint; numeric IP only, port 1..65535. */
SE_API se_status SE_CALL se_game_listen(se_game_handle endpoint,
    const char* bind_address, uint32_t port, se_error* error);
/* Nonblocking connect. Wait for CONNECTED before send; failure is DISCONNECTED.
 * Client endpoints can connect without listen. Numeric IP only. */
SE_API se_status SE_CALL se_game_connect(se_game_handle endpoint,
    const char* address, uint32_t port, uint64_t* peer_id, se_error* error);
/* RELIABLE_ORDERED is ordered within its connection's reliable stream.
 * UNRELIABLE may be lost, duplicated or reordered: add game tick/sequence IDs
 * to snapshots. Neither mode is a business transaction or durable receipt.
 * SE_OK means copied/queued. BACKPRESSURE means not accepted: retry later.
 * Queue overflow is terminal. Destroy and create a new endpoint to recover. */
SE_API se_status SE_CALL se_game_send(se_game_handle endpoint, uint64_t peer_id,
    uint32_t delivery, const void* data, uint32_t size, se_error* error);
/* Pump frequently on server AND client. One consumer per endpoint recommended.
 * BUFFER_TOO_SMALL keeps the event queued. timeout_ms=0 is nonblocking.
 * Poll buffers are caller-owned. Close/destroy wakes waiting poll calls. */
SE_API se_status SE_CALL se_game_poll(se_game_handle endpoint,
    se_game_event* event, void* payload, uint32_t capacity, uint32_t timeout_ms,
    se_error* error);
SE_API se_status SE_CALL se_game_disconnect(se_game_handle endpoint,
    uint64_t peer_id, se_error* error);
/* Cancels pending sends; no delivery/drain guarantee. Call only after the app
 * has confirmed important commands if it needs a graceful business shutdown.
 * Never unload ServerEngine.dll until all calls and endpoints have ended. */
SE_API se_status SE_CALL se_game_destroy(se_game_handle endpoint, se_error* error);

#ifdef __cplusplus
}
#endif
#endif
