#include <ServerEngine/C/Redis.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}
}

int main()
{
    se_redis_handle redis = 0;
    try {
        require(se_get_abi_version() == SE_ABI_VERSION, "ABI version");
        se_redis_options options;
        se_redis_options_init(&options);
        se_error error{};
        require(options.security == SE_SECURITY_TLS, "plaintext requires explicit opt-in");
        require(se_redis_open(&options, &redis, &error) == SE_NOT_SUPPORTED && redis == 0,
            "unimplemented TLS must fail closed");
        require(se_redis_open(nullptr, &redis, &error) == SE_INVALID_ARGUMENT && redis == 0,
            "null options");
        options.security = SE_SECURITY_NONE;
        options.reserved[0] = 1;
        require(se_redis_open(&options, &redis, &error) == SE_INVALID_ARGUMENT, "reserved field");
        options.reserved[0] = 0;
        options.address = "localhost";
        require(se_redis_open(&options, &redis, &error) == SE_INVALID_ARGUMENT,
            "hostname rejected to bound connection cancellation");
        options.address = "127.0.0.1";
        options.username = "user";
        require(se_redis_open(&options, &redis, &error) == SE_INVALID_ARGUMENT, "ACL user needs password");
        options.username = nullptr;
        options.max_result_bytes = options.max_value_bytes - 1;
        require(se_redis_open(&options, &redis, &error) == SE_INVALID_ARGUMENT, "result reservation limit");
        options.max_result_bytes = options.max_value_bytes;
        const auto opened = se_redis_open(&options, &redis, &error);
        require(opened == SE_OK || opened == SE_NOT_SUPPORTED, "optional connector open");
        if (opened == SE_OK) {
            // No endpoint is contacted until a job is submitted.
            std::uint64_t request = 123;
            require(se_redis_get(redis, nullptr, 1, &request, &error) == SE_INVALID_ARGUMENT && request == 0,
                "invalid input not submitted");
            se_redis_result result;
            se_redis_result_init(&result);
            require(se_redis_poll(redis, &result, nullptr, 0, 0, &error) == SE_TIMEOUT, "empty poll");
            require(se_redis_stop(redis, &error) == SE_OK, "stop before first connect");
            require(se_redis_stop(redis, &error) == SE_OK, "idempotent stop");
            require(se_redis_poll(redis, &result, nullptr, 0, 0, &error) == SE_STOPPED, "stopped poll");
            require(se_redis_get(redis, "key", 3, &request, &error) == SE_STOPPED && request == 0, "terminal stop");
            const auto stale = redis;
            require(se_redis_destroy(redis, &error) == SE_OK, "destroy");
            redis = 0;
            require(se_redis_open(&options, &redis, &error) == SE_OK && redis != stale, "tokens never reused");
            require(se_redis_stop(stale, &error) == SE_INVALID_HANDLE, "stale handle cannot stop replacement");
            require(se_redis_stop(UINT64_C(1), &error) == SE_INVALID_HANDLE, "server handle cannot alias Redis token");
            require(se_redis_destroy(redis, &error) == SE_OK, "destroy replacement");
            redis = 0;
        }
        std::cout << "PASS Redis ABI contracts\n";
        return 0;
    } catch (const std::exception& error) {
        if (redis != 0) se_redis_destroy(redis, nullptr);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
