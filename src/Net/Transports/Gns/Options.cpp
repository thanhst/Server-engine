#include "Options.h"

#include <algorithm>
#include <iterator>

namespace serverengine::net::gns {

uint32_t native_receive_bytes(const se_datagram_options& options) noexcept
{
    // Per native connection; separate from the endpoint's application event queue.
    // GNS requires this value to exceed RecvMaxMessageSize.
    return (std::max)(4096u, options.max_message_bytes * 2);
}

uint32_t native_receive_count(const se_datagram_options& options) noexcept
{
    // GNS 1.6 requires at least two native receive slots, even if the host
    // deliberately configures a one-event application queue for overflow tests.
    return (std::max)(2u, (std::min)(1024u, options.max_event_queue_count));
}

bool valid_options(const se_datagram_options* options) noexcept
{
    if (options == nullptr || options->struct_size != sizeof(*options)
        || options->abi_version != SE_DATAGRAM_ABI_VERSION
        || (options->flags & ~SE_DATAGRAM_ALLOW_REMOTE_UNAUTHENTICATED) != 0
        || std::any_of(std::begin(options->reserved), std::end(options->reserved),
            [](uint64_t value) { return value != 0; })) return false;

    if (options->max_peers == 0 || options->max_peers > 1024
        || options->max_message_bytes < 64 || options->max_message_bytes > 512 * 1024
        || options->max_send_queue_bytes < 4096
        || options->max_send_queue_bytes < options->max_message_bytes
        || options->max_send_queue_bytes > 16 * 1024 * 1024
        || options->max_event_queue_count == 0 || options->max_event_queue_count > 65536
        || options->max_event_queue_bytes < sizeof(se_datagram_event) + options->max_message_bytes
        || options->max_event_queue_bytes > UINT64_C(256) * 1024 * 1024
        || options->connect_timeout_ms < 100 || options->connect_timeout_ms > 60000) return false;

    // This bounds configured native payload queues, not all library/OS overhead.
    const auto per_peer = uint64_t(options->max_send_queue_bytes) + native_receive_bytes(*options);
    return per_peer * options->max_peers <= UINT64_C(512) * 1024 * 1024;
}

se_datagram_event empty_event() noexcept
{
    se_datagram_event value{};
    value.struct_size = sizeof(value);
    value.abi_version = SE_DATAGRAM_ABI_VERSION;
    return value;
}

} // namespace serverengine::net::gns
