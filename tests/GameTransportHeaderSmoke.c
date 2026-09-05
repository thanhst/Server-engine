#include <ServerEngine/C/GameTransport.h>

/* Compile as C: the public protocol must not require any C++ or vendor types. */
int main(void)
{
    se_game_options options;
    se_game_event event;
    se_game_handle endpoint = 0;
    se_error error;
    se_status status;
    if (se_game_get_abi_version() != SE_GAME_ABI_VERSION) return 1;
    se_game_options_init(&options);
    se_game_event_init(&event);
    if (sizeof(options) != 72 || sizeof(event) != 192) return 2;
    if (options.struct_size != sizeof(options) || event.struct_size != sizeof(event)) return 3;
    status = se_game_create(&options, &endpoint, &error);
    if (status == SE_NOT_SUPPORTED) return endpoint == 0 ? 0 : 4;
    if (status != SE_OK || endpoint == 0) return 5;
    return se_game_destroy(endpoint, &error) == SE_OK ? 0 : 6;
}
