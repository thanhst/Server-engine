#include <ServerEngine/C/GameTransport.h>

// Keep the published v1 symbols for existing binaries. All behavior lives in
// DatagramTransport.cpp, so old and new callers share handles, queues and errors.
uint32_t SE_CALL se_game_get_abi_version(void)
{ return se_datagram_get_abi_version(); }
void SE_CALL se_game_options_init(se_game_options* options)
{ se_datagram_options_init(options); }
void SE_CALL se_game_event_init(se_game_event* event)
{ se_datagram_event_init(event); }
se_status SE_CALL se_game_create(const se_game_options* options, se_game_handle* endpoint, se_error* error)
{ return se_datagram_create(options, endpoint, error); }
se_status SE_CALL se_game_listen(se_game_handle endpoint, const char* address, uint32_t port, se_error* error)
{ return se_datagram_listen(endpoint, address, port, error); }
se_status SE_CALL se_game_connect(se_game_handle endpoint, const char* address, uint32_t port,
    uint64_t* peer, se_error* error)
{ return se_datagram_connect(endpoint, address, port, peer, error); }
se_status SE_CALL se_game_send(se_game_handle endpoint, uint64_t peer, uint32_t delivery,
    const void* data, uint32_t size, se_error* error)
{ return se_datagram_send(endpoint, peer, delivery, data, size, error); }
se_status SE_CALL se_game_poll(se_game_handle endpoint, se_game_event* event,
    void* payload, uint32_t capacity, uint32_t timeout_ms, se_error* error)
{ return se_datagram_poll(endpoint, event, payload, capacity, timeout_ms, error); }
se_status SE_CALL se_game_disconnect(se_game_handle endpoint, uint64_t peer, se_error* error)
{ return se_datagram_disconnect(endpoint, peer, error); }
se_status SE_CALL se_game_destroy(se_game_handle endpoint, se_error* error)
{ return se_datagram_destroy(endpoint, error); }
