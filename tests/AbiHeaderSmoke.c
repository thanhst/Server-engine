#include <ServerEngine/C/ServerEngine.h>
#include <ServerEngine/C/Http.h>
#include <ServerEngine/C/Sql.h>
#include <ServerEngine/C/Redis.h>

/* Compiled as C, never C++. Detects accidental C++ types in the public ABI. */
int main(void)
{
    se_server_options options;
    se_event event;
    se_server_handle server = 0;
    se_error error;
    se_http_response response;
    se_sql_options sql_options;
    se_redis_options redis_options;
    if (se_get_abi_version() != SE_ABI_VERSION) return 1;
    if (se_sql_get_abi_version() != SE_SQL_ABI_VERSION) return 5;
    se_http_response_init(&response);
    se_sql_options_init(&sql_options);
    se_redis_options_init(&redis_options);
    if (response.struct_size != sizeof(response) || sql_options.struct_size != sizeof(sql_options)
        || redis_options.struct_size != sizeof(redis_options)) return 6;
    se_server_options_init(&options);
    se_event_init(&event);
    if (sizeof(se_error) != 256 || sizeof(se_event) != 112) return 2;
    if (se_server_create(&options, &server, &error) != SE_OK) return 3;
    return se_server_destroy(server, &error) == SE_OK ? 0 : 4;
}
