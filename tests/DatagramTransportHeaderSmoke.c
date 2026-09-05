#include <ServerEngine/C/DatagramTransport.h>
#include <ServerEngine/C/GameTransport.h>

#include <stddef.h>

/* Legacy names must describe the same ABI, including explicit C struct tags. */
_Static_assert(_Generic((se_game_options*)0, se_datagram_options*: 1, default: 0),
    "Legacy and canonical options are the same C type");
_Static_assert(_Generic((struct se_game_options*)0, struct se_datagram_options*: 1, default: 0),
    "Legacy options struct tag remains source compatible");
_Static_assert(_Generic((struct se_game_event*)0, struct se_datagram_event*: 1, default: 0),
    "Legacy event struct tag remains source compatible");
_Static_assert(sizeof(se_game_handle) == sizeof(se_datagram_handle), "Handle layout is unchanged");
_Static_assert(sizeof(se_datagram_options) == 72, "Options ABI layout is unchanged");
_Static_assert(sizeof(se_datagram_event) == 192, "Event ABI layout is unchanged");
_Static_assert(offsetof(se_game_event, payload_size) == offsetof(se_datagram_event, payload_size),
    "Legacy and canonical event fields have the same offsets");

/* Compile as C: the public protocol must not require any C++ or vendor types. */
int main(void)
{
    se_datagram_options options;
    struct se_game_options legacy_options;
    se_datagram_event event;
    struct se_game_event legacy_event;
    se_datagram_handle endpoint = 0;
    se_game_handle legacy_endpoint = 0;
    se_error error;
    se_status status;
    se_status legacy_status;
    if (se_datagram_get_abi_version() != SE_DATAGRAM_ABI_VERSION) return 1;
    if (se_game_get_abi_version() != se_datagram_get_abi_version()) return 2;
    se_datagram_options_init(&options);
    se_datagram_event_init(&event);
    se_game_options_init(&legacy_options);
    se_game_event_init(&legacy_event);
    if (options.struct_size != sizeof(options) || event.struct_size != sizeof(event)) return 3;
    if (legacy_options.struct_size != options.struct_size || legacy_event.struct_size != event.struct_size) return 4;
    legacy_status = se_game_create(&legacy_options, &legacy_endpoint, &error);
    status = se_datagram_create(&options, &endpoint, &error);
    if (status == SE_NOT_SUPPORTED && legacy_status == SE_NOT_SUPPORTED)
        return endpoint == 0 && legacy_endpoint == 0 ? 0 : 5;
    if (status != SE_OK || legacy_status != SE_OK || endpoint == 0 || legacy_endpoint == 0) {
        if (endpoint != 0) se_datagram_destroy(endpoint, &error);
        if (legacy_endpoint != 0) se_game_destroy(legacy_endpoint, &error);
        return 6;
    }
    /* Both symbol families must use the same endpoint registry in the DLL. */
    legacy_status = se_datagram_destroy(legacy_endpoint, &error);
    status = se_game_destroy(endpoint, &error);
    return status == SE_OK && legacy_status == SE_OK ? 0 : 7;
}
