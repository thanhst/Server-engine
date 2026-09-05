#include "Boundary.h"
#include "Net/Transports/Gns/Api.h"
#include "Net/Transports/Gns/Options.h"

namespace {
namespace gns = serverengine::net::gns;
const char* description(se_status status)
{
    switch (status) {
    case SE_NOT_SUPPORTED: return "Datagram transport disabled; configure SE_WITH_GNS=ON";
    case SE_INVALID_ARGUMENT: return "Invalid datagram transport argument, ABI layout, address or limit";
    case SE_INVALID_HANDLE: return "Unknown datagram endpoint or peer handle";
    case SE_INVALID_STATE: return "Datagram peer not connected or endpoint already listening";
    case SE_BACKPRESSURE: return "Datagram capacity full; message not accepted, poll and retry later";
    case SE_RESULT_TOO_LARGE: return "Datagram message exceeds endpoint limit";
    case SE_IO_ERROR: return "GNS could not open/configure transport; check address, port and dependency version";
    default: return "Datagram transport internal failure";
    }
}
template<class Function>
se_status boundary(se_error* error, Function operation)
{
    return serverengine::abi::protect(error, [&] {
        const auto status = operation();
        return status < 0 ? serverengine::abi::fail(error, status, description(status)) : status;
    });
}
}

uint32_t SE_CALL se_datagram_get_abi_version(void) { return SE_DATAGRAM_ABI_VERSION; }
void SE_CALL se_datagram_options_init(se_datagram_options* options)
{
    if (!options) return;
    *options = {};
    options->struct_size = sizeof(*options);
    options->abi_version = SE_DATAGRAM_ABI_VERSION;
    options->max_peers = 64;
    options->max_message_bytes = 64 * 1024;
    options->max_send_queue_bytes = 256 * 1024;
    options->max_event_queue_count = 4096;
    options->max_event_queue_bytes = 8 * 1024 * 1024;
    options->connect_timeout_ms = 10000;
}
void SE_CALL se_datagram_event_init(se_datagram_event* event) { if (event) *event = gns::empty_event(); }

se_status SE_CALL se_datagram_create(const se_datagram_options* options, se_datagram_handle* endpoint, se_error* error)
{
    if (endpoint) *endpoint = 0;
    return boundary(error, [&]() -> se_status {
        if (!endpoint || !gns::valid_options(options)) return SE_INVALID_ARGUMENT;
#ifdef SERVERENGINE_WITH_GNS
        return gns::create(*options, *endpoint);
#else
        return SE_NOT_SUPPORTED;
#endif
    });
}
se_status SE_CALL se_datagram_listen(se_datagram_handle endpoint, const char* address, uint32_t port, se_error* error)
{
    return boundary(error, [&]() -> se_status {
#ifdef SERVERENGINE_WITH_GNS
        return gns::listen(endpoint, address, port);
#else
        (void)endpoint; (void)address; (void)port;
        return SE_NOT_SUPPORTED;
#endif
    });
}
se_status SE_CALL se_datagram_connect(se_datagram_handle endpoint, const char* address, uint32_t port,
    uint64_t* peer, se_error* error)
{
    if (peer) *peer = 0;
    return boundary(error, [&]() -> se_status {
        if (!peer) return SE_INVALID_ARGUMENT;
#ifdef SERVERENGINE_WITH_GNS
        return gns::connect(endpoint, address, port, *peer);
#else
        (void)endpoint; (void)address; (void)port;
        return SE_NOT_SUPPORTED;
#endif
    });
}
se_status SE_CALL se_datagram_send(se_datagram_handle endpoint, uint64_t peer, uint32_t delivery,
    const void* data, uint32_t size, se_error* error)
{
    return boundary(error, [&]() -> se_status {
        if ((!data && size) || (delivery != SE_DATAGRAM_UNRELIABLE && delivery != SE_DATAGRAM_RELIABLE_ORDERED))
            return SE_INVALID_ARGUMENT;
#ifdef SERVERENGINE_WITH_GNS
        return gns::send(endpoint, peer, delivery, data, size);
#else
        (void)endpoint; (void)peer;
        return SE_NOT_SUPPORTED;
#endif
    });
}
se_status SE_CALL se_datagram_poll(se_datagram_handle endpoint, se_datagram_event* event,
    void* payload, uint32_t capacity, uint32_t timeout_ms, se_error* error)
{
    return boundary(error, [&]() -> se_status {
        if (!event || event->struct_size != sizeof(*event) || event->abi_version != SE_DATAGRAM_ABI_VERSION ||
            (!payload && capacity)) return SE_INVALID_ARGUMENT;
        *event = gns::empty_event();
#ifdef SERVERENGINE_WITH_GNS
        return gns::poll(endpoint, *event, payload, capacity, timeout_ms);
#else
        (void)endpoint; (void)timeout_ms;
        return SE_NOT_SUPPORTED;
#endif
    });
}
se_status SE_CALL se_datagram_disconnect(se_datagram_handle endpoint, uint64_t peer, se_error* error)
{
    return boundary(error, [&]() -> se_status {
#ifdef SERVERENGINE_WITH_GNS
        return gns::disconnect(endpoint, peer);
#else
        (void)endpoint; (void)peer;
        return SE_NOT_SUPPORTED;
#endif
    });
}
se_status SE_CALL se_datagram_destroy(se_datagram_handle endpoint, se_error* error)
{
    return boundary(error, [&]() -> se_status {
#ifdef SERVERENGINE_WITH_GNS
        return gns::destroy(endpoint);
#else
        (void)endpoint;
        return SE_NOT_SUPPORTED;
#endif
    });
}
