#pragma once

#include <ServerEngine/C/DatagramTransport.h>

namespace serverengine::net::gns {

bool valid_options(const se_datagram_options* options) noexcept;
uint32_t native_receive_bytes(const se_datagram_options& options) noexcept;
uint32_t native_receive_count(const se_datagram_options& options) noexcept;
se_datagram_event empty_event() noexcept;

} // namespace serverengine::net::gns
