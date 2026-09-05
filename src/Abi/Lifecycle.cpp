#include "Boundary.h"
#include "HandleRegistry.h"
#include "Options.h"

using namespace serverengine;

uint32_t SE_CALL se_get_abi_version(void) { return SE_ABI_VERSION; }

void SE_CALL se_server_options_init(se_server_options* options)
{
    if (options != nullptr) {
        *options = {};
        options->struct_size = sizeof(*options);
        options->abi_version = SE_ABI_VERSION;
        options->max_connections = 4096;
        options->max_message_bytes = 65536;
        options->max_send_queue_bytes = 1024 * 1024;
        options->max_event_queue_count = 8192;
        options->max_event_queue_bytes = 16 * 1024 * 1024;
        options->idle_timeout_ms = 300000;
    }
}

void SE_CALL se_listener_options_init(se_listener_options* options)
{
    if (options != nullptr) {
        *options = {};
        options->struct_size = sizeof(*options);
        options->abi_version = SE_ABI_VERSION;
        options->protocol = SE_PROTOCOL_TCP;
        options->security = SE_SECURITY_TLS;
        options->handshake_timeout_ms = 10000;
    }
}

void SE_CALL se_event_init(se_event* event)
{
    if (event != nullptr) {
        *event = {};
        event->struct_size = sizeof(*event);
        event->abi_version = SE_ABI_VERSION;
    }
}

se_status SE_CALL se_server_create(const se_server_options* options, se_server_handle* server, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        if (server == nullptr) {
            return abi::fail(error, SE_INVALID_ARGUMENT, "output server handle is null");
        }
        *server = 0;
        runtime::host::HostOptions decoded;
        std::string detail;
        if (!abi::decode(options, decoded, detail)) {
            return abi::fail(error, SE_INVALID_ARGUMENT, detail);
        }
        *server = abi::register_host(std::make_shared<runtime::host::ServerHost>(std::move(decoded)));
        return SE_OK;
    });
}

se_status SE_CALL se_server_add_listener(se_server_handle server, const se_listener_options* options,
    uint64_t* listener_id, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        if (listener_id == nullptr) {
            return abi::fail(error, SE_INVALID_ARGUMENT, "output listener ID is null");
        }
        *listener_id = 0;
        const auto host = abi::find_host(server);
        if (!host) {
            return abi::fail(error, SE_INVALID_HANDLE, "unknown server handle");
        }
        net::ListenerConfig decoded;
        std::string detail;
        if (!abi::decode(options, decoded, detail)) {
            return abi::fail(error, SE_INVALID_ARGUMENT, detail);
        }
        if (!host->add_listener(std::move(decoded), *listener_id, detail)) {
            return abi::fail(error, SE_INVALID_STATE, detail);
        }
        return SE_OK;
    });
}

se_status SE_CALL se_server_start(se_server_handle server, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        const auto host = abi::find_host(server);
        if (!host) {
            return abi::fail(error, SE_INVALID_HANDLE, "unknown server handle");
        }
        std::string detail;
        return host->start(detail) ? SE_OK : abi::fail(error, SE_IO_ERROR, detail);
    });
}

se_status SE_CALL se_server_stop(se_server_handle server, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        const auto host = abi::find_host(server);
        if (!host) {
            return abi::fail(error, SE_INVALID_HANDLE, "unknown server handle");
        }
        host->stop();
        return SE_OK;
    });
}

se_status SE_CALL se_server_destroy(se_server_handle server, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        const auto host = abi::remove_host(server);
        if (!host) {
            return abi::fail(error, SE_INVALID_HANDLE, "unknown server handle");
        }
        host->stop();
        return SE_OK;
    });
}
