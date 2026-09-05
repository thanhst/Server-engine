#pragma once

#include <ServerEngine/C/DatagramTransport.h>

namespace serverengine::net::gns {

// Internal entry points. The C boundary owns exception translation and diagnostics.
se_status create(const se_datagram_options& options, se_datagram_handle& output);
se_status listen(se_datagram_handle endpoint, const char* address, uint32_t port);
se_status connect(se_datagram_handle endpoint, const char* address, uint32_t port, uint64_t& peer);
se_status send(se_datagram_handle endpoint, uint64_t peer, uint32_t delivery, const void* data, uint32_t size);
se_status poll(se_datagram_handle endpoint, se_datagram_event& event, void* payload,
    uint32_t capacity, uint32_t timeout_ms);
se_status disconnect(se_datagram_handle endpoint, uint64_t peer);
se_status destroy(se_datagram_handle endpoint);

} // namespace serverengine::net::gns
