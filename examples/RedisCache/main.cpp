#include <ServerEngine/C/Redis.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
class RedisHandle {
public:
    ~RedisHandle() { if (value != 0) se_redis_destroy(value, nullptr); }
    se_redis_handle value = 0;
};

void check(se_status status, const se_error& error)
{
    if (status != SE_OK) throw std::runtime_error(error.message[0] ? error.message : "Redis API failed");
}

se_redis_result completion(se_redis_handle redis, std::uint64_t request,
    void* value = nullptr, std::uint32_t capacity = 0)
{
    se_redis_result result;
    se_redis_result_init(&result);
    se_error error{};
    check(se_redis_poll(redis, &result, value, capacity, 10000, &error), error);
    std::cout << "request=" << result.request_id << " operation=" << result.operation
              << " status=" << result.status << '\n';
    if (result.request_id != request) throw std::runtime_error("Unexpected completion ID");
    if (result.status != SE_OK) throw std::runtime_error(result.message);
    return result;
}
}

int main(int argc, char** argv)
{
    try {
        if (argc != 4 || std::string_view(argv[1]) != "--allow-plaintext") {
            std::cerr << "Usage: ServerEngineRedisCache --allow-plaintext <numeric-address> <port>\n"
                         "Only use a trusted local/private endpoint or a separately secured tunnel.\n"
                         "Credentials and data are NOT encrypted by this connector.\n"
                         "Optional credentials: SE_REDIS_USERNAME and SE_REDIS_PASSWORD environment variables.\n";
            return 2;
        }
        if (se_get_abi_version() != SE_ABI_VERSION) throw std::runtime_error("Incompatible ServerEngine DLL ABI");
        std::uint32_t port = 0;
        const std::string_view port_text(argv[3]);
        const auto parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() + port_text.size())
            throw std::runtime_error("Invalid port");

        se_redis_options options;
        se_redis_options_init(&options);
        options.address = argv[2];
        options.port = port;
        options.security = SE_SECURITY_NONE;
        options.username = std::getenv("SE_REDIS_USERNAME");
        options.password = std::getenv("SE_REDIS_PASSWORD");
        RedisHandle redis;
        se_error error{};
        check(se_redis_open(&options, &redis.value, &error), error);

        constexpr std::string_view key = "serverengine:example:binary-cache";
        const std::array<std::uint8_t, 5> value{0x00, 0x41, 0xff, 0x0a, 0x42};
        std::uint64_t request = 0;
        check(se_redis_set(redis.value, key.data(), static_cast<std::uint32_t>(key.size()),
            value.data(), static_cast<std::uint32_t>(value.size()), 30000, &request, &error), error);
        completion(redis.value, request); // Real application polls in its own event loop.

        check(se_redis_get(redis.value, key.data(), static_cast<std::uint32_t>(key.size()), &request, &error), error);
        std::array<std::uint8_t, 5> received{};
        const auto result = completion(redis.value, request, received.data(), static_cast<std::uint32_t>(received.size()));
        if (!result.found || result.value_size != value.size() || received != value)
            throw std::runtime_error("Cache value changed, expired, or failed its binary roundtrip");
        std::cout << "Stored and read 5 binary bytes; key expires 30 seconds after SET.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
