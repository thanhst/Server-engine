#ifndef SERVERENGINE_GAME_TRANSPORT_H
#define SERVERENGINE_GAME_TRANSPORT_H

/* Compatibility names only. New code should include DatagramTransport.h.
 * Keep both old typedef spellings and "struct se_game_options/event" working.
 * Type macros resolve to the same layouts; no copies or pointer casts are needed.
 * se_game_* symbols remain real DLL exports forwarding to se_datagram_*. */
#include <ServerEngine/C/DatagramTransport.h>

#define se_game_handle se_datagram_handle
#define se_game_options se_datagram_options
#define se_game_event se_datagram_event
#define SE_GAME_ABI_VERSION SE_DATAGRAM_ABI_VERSION
#define SE_GAME_UNRELIABLE SE_DATAGRAM_UNRELIABLE
#define SE_GAME_RELIABLE_ORDERED SE_DATAGRAM_RELIABLE_ORDERED
#define SE_GAME_CONNECTED SE_DATAGRAM_CONNECTED
#define SE_GAME_MESSAGE SE_DATAGRAM_MESSAGE
#define SE_GAME_DISCONNECTED SE_DATAGRAM_DISCONNECTED
#define SE_GAME_OVERFLOW SE_DATAGRAM_OVERFLOW
#define SE_GAME_ALLOW_REMOTE_UNAUTHENTICATED SE_DATAGRAM_ALLOW_REMOTE_UNAUTHENTICATED
#ifdef __cplusplus
extern "C" {
#endif

SE_API uint32_t SE_CALL se_game_get_abi_version(void);
SE_API void SE_CALL se_game_options_init(se_game_options* options);
SE_API void SE_CALL se_game_event_init(se_game_event* event);
SE_API se_status SE_CALL se_game_create(const se_game_options* options,
    se_game_handle* endpoint, se_error* error);
SE_API se_status SE_CALL se_game_listen(se_game_handle endpoint,
    const char* bind_address, uint32_t port, se_error* error);
SE_API se_status SE_CALL se_game_connect(se_game_handle endpoint,
    const char* address, uint32_t port, uint64_t* peer_id, se_error* error);
SE_API se_status SE_CALL se_game_send(se_game_handle endpoint, uint64_t peer_id,
    uint32_t delivery, const void* data, uint32_t size, se_error* error);
SE_API se_status SE_CALL se_game_poll(se_game_handle endpoint,
    se_game_event* event, void* payload, uint32_t capacity, uint32_t timeout_ms,
    se_error* error);
SE_API se_status SE_CALL se_game_disconnect(se_game_handle endpoint,
    uint64_t peer_id, se_error* error);
SE_API se_status SE_CALL se_game_destroy(se_game_handle endpoint, se_error* error);
#ifdef __cplusplus
}
#endif
#endif
