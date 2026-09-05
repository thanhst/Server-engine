#include <ServerEngine/C/ServerEngine.h>

#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}

int main()
{
    se_server_handle server = 0;
    try {
        require(se_get_abi_version() == SE_ABI_VERSION, "ABI version");
        se_server_options options;
        se_server_options_init(&options);
        se_error error{};
        require(se_server_create(nullptr, &server, &error) == SE_INVALID_ARGUMENT && server == 0, "null options");
        options.abi_version++;
        require(se_server_create(&options, &server, &error) == SE_INVALID_ARGUMENT, "wrong ABI rejected");
        se_server_options_init(&options);
        options.reserved[2] = 1;
        require(se_server_create(&options, &server, &error) == SE_INVALID_ARGUMENT, "reserved bits rejected");
        se_server_options_init(&options);
        require(se_server_create(&options, &server, &error) == SE_OK && server != 0, "create");
        require(se_server_start(server, &error) == SE_IO_ERROR, "empty listener set fails");

        se_listener_options listener;
        se_listener_options_init(&listener);
        listener.protocol = SE_PROTOCOL_UDP;
        listener.port = 19000;
        uint64_t listener_id = 123;
        require(se_server_add_listener(server, &listener, &listener_id, &error) == SE_INVALID_ARGUMENT
            && listener_id == 0, "TLS on UDP must fail closed");
        listener.protocol = SE_PROTOCOL_TCP;
        require(se_server_add_listener(server, &listener, &listener_id, &error) == SE_INVALID_ARGUMENT,
            "TLS cannot silently fall back without certificate");

        se_event event;
        se_event_init(&event);
        require(se_server_poll_event(server, &event, nullptr, 0, 0, &error) == SE_TIMEOUT, "empty poll");
        require(se_server_send(server, 1, nullptr, 1, &error) == SE_INVALID_ARGUMENT, "null data with nonzero size");
        require(se_server_stop(server, &error) == SE_OK, "stop before start");
        require(se_server_stop(server, &error) == SE_OK, "stop idempotent");
        require(se_server_poll_event(server, &event, nullptr, 0, 0, &error) == SE_STOPPED, "stop wakes poll");
        require(se_server_destroy(server, &error) == SE_OK, "destroy");
        require(se_server_destroy(server, &error) == SE_INVALID_HANDLE, "stale handle safe");
        const auto stale = server;
        require(se_server_create(&options, &server, &error) == SE_OK && server != stale, "handle never reused");
        require(se_server_stop(stale, &error) == SE_INVALID_HANDLE, "stale handle cannot control replacement");
        require(se_server_destroy(server, &error) == SE_OK, "destroy replacement");
        server = 0;
        std::cout << "PASS C ABI\n";
        return 0;
    } catch (const std::exception& error) {
        if (server != 0) se_server_destroy(server, nullptr);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
