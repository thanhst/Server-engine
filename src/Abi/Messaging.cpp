#include "Boundary.h"
#include "HandleRegistry.h"

using namespace serverengine;

se_status SE_CALL se_server_send(se_server_handle server, uint64_t session_id,
    const void* data, uint32_t size, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        if ((data == nullptr && size != 0) || session_id == 0) {
            return abi::fail(error, SE_INVALID_ARGUMENT, "invalid message pointer or session ID");
        }
        const auto host = abi::find_host(server);
        if (!host) {
            return abi::fail(error, SE_INVALID_HANDLE, "unknown server handle");
        }
        if (size > host->max_message_bytes()) {
            return abi::fail(error, SE_INVALID_ARGUMENT, "message exceeds configured limit");
        }
        if (host->overflowed()) {
            return abi::fail(error, SE_BACKPRESSURE, "event queue overflowed; poll terminal event and recreate server");
        }
        std::string detail;
        const core::Buffer buffer(static_cast<const core::Buffer::Byte*>(data), size);
        return host->send(session_id, buffer, detail) ? SE_OK : abi::fail(error, SE_IO_ERROR, detail);
    });
}

se_status SE_CALL se_server_disconnect(se_server_handle server, uint64_t session_id, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        const auto host = abi::find_host(server);
        if (!host) {
            return abi::fail(error, SE_INVALID_HANDLE, "unknown server handle");
        }
        std::string detail;
        return host->disconnect(session_id, detail) ? SE_OK : abi::fail(error, SE_IO_ERROR, detail);
    });
}

se_status SE_CALL se_server_poll_event(se_server_handle server, se_event* event,
    void* payload, uint32_t capacity, uint32_t timeout_ms, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        if (event == nullptr || event->struct_size != sizeof(*event) || event->abi_version != SE_ABI_VERSION
            || (payload == nullptr && capacity != 0)) {
            return abi::fail(error, SE_INVALID_ARGUMENT, "invalid event header or payload buffer");
        }
        const auto host = abi::find_host(server);
        if (!host) {
            return abi::fail(error, SE_INVALID_HANDLE, "unknown server handle");
        }
        runtime::host::Event next;
        const auto result = host->poll(next, payload, capacity, timeout_ms);
        if (result == runtime::host::PollResult::Timeout) {
            return SE_TIMEOUT;
        }
        if (result == runtime::host::PollResult::Stopped) {
            return SE_STOPPED;
        }
        se_event_init(event);
        event->kind = static_cast<std::uint32_t>(next.kind);
        event->protocol = static_cast<std::uint32_t>(next.peer.protocol);
        event->session_id = next.peer.session_id;
        event->listener_id = next.peer.listener_id;
        event->sequence = next.sequence;
        event->payload_size = static_cast<std::uint32_t>(next.payload_size);
        event->peer_port = next.peer.port;
        const auto address_size = (std::min)(next.peer.address.size(), sizeof(event->peer_address) - 1);
        std::memcpy(event->peer_address, next.peer.address.data(), address_size);
        return result == runtime::host::PollResult::BufferTooSmall ? SE_BUFFER_TOO_SMALL : SE_OK;
    });
}
