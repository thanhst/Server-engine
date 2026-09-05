#include <ServerEngine/C/Redis.h>

#include "Boundary.h"
#include "Data/Redis/Service.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {
using serverengine::abi::fail;
using serverengine::abi::protect;
using serverengine::data::redis::Connection;
using serverengine::data::redis::Options;
using serverengine::data::redis::Service;

struct Registry {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<Service>> services;
    std::uint64_t next_id = 1;
};

Registry& registry()
{
    static Registry value;
    return value;
}

std::shared_ptr<Service> find(se_redis_handle handle)
{
    auto& entries = registry();
    std::lock_guard lock(entries.mutex);
    const auto found = entries.services.find(handle);
    return found == entries.services.end() ? nullptr : found->second;
}

bool copy_string(const char* source, std::size_t limit, std::string& output)
{
    if (source == nullptr) return true;
    std::size_t size = 0;
    while (size <= limit && source[size] != '\0') ++size;
    if (size > limit) return false;
    output.assign(source, size);
    return true;
}

se_status decode(const se_redis_options* input, Options& output, se_error* error)
{
    if (input == nullptr || input->struct_size != sizeof(*input) || input->abi_version != SE_ABI_VERSION
        || input->reserved32 != 0 || std::any_of(std::begin(input->reserved), std::end(input->reserved),
            [](std::uint64_t value) { return value != 0; }))
        return fail(error, SE_INVALID_ARGUMENT, "invalid Redis options layout, version or reserved fields");
    if (input->security == SE_SECURITY_TLS)
        return fail(error, SE_NOT_SUPPORTED, "Redis TLS is not implemented; no plaintext fallback");
    if (input->security != SE_SECURITY_NONE || input->port == 0 || input->port > 65535
        || input->connect_timeout_ms < 100 || input->connect_timeout_ms > 30000
        || input->command_timeout_ms < 100 || input->command_timeout_ms > 30000
        || input->max_inflight_requests == 0 || input->max_inflight_requests > 65536
        || input->max_key_bytes == 0 || input->max_key_bytes > 65536
        || input->max_value_bytes == 0 || input->max_value_bytes > 16 * 1024 * 1024
        || input->max_queue_bytes < static_cast<std::uint64_t>(input->max_key_bytes) + input->max_value_bytes
        || input->max_queue_bytes > UINT64_C(1024) * 1024 * 1024
        || input->max_result_bytes < input->max_value_bytes
        || input->max_result_bytes > UINT64_C(1024) * 1024 * 1024)
        return fail(error, SE_INVALID_ARGUMENT, "invalid Redis endpoint, timeout or queue limits");
    if (!copy_string(input->address, 64, output.address) || output.address.empty()
        || !Connection::valid_address(output.address)
        || !copy_string(input->username, 256, output.username)
        || !copy_string(input->password, 4096, output.password)
        || (!output.username.empty() && output.password.empty()))
        return fail(error, SE_INVALID_ARGUMENT, "invalid Redis numeric address or credential configuration");
    output.port = static_cast<std::uint16_t>(input->port);
    output.connect_timeout_ms = input->connect_timeout_ms;
    output.command_timeout_ms = input->command_timeout_ms;
    output.max_inflight_requests = input->max_inflight_requests;
    output.max_key_bytes = input->max_key_bytes;
    output.max_value_bytes = input->max_value_bytes;
    output.max_queue_bytes = input->max_queue_bytes;
    output.max_result_bytes = input->max_result_bytes;
    return SE_OK;
}

se_status submit(se_redis_handle handle, std::uint32_t operation, const void* key,
    std::uint32_t key_size, const void* value, std::uint32_t value_size,
    std::uint64_t ttl_ms, std::uint64_t* request_id, se_error* error)
{
    if (request_id != nullptr) *request_id = 0;
    return protect(error, [&]() -> se_status {
        if (request_id == nullptr) return fail(error, SE_INVALID_ARGUMENT, "request_id output is required");
        const auto service = find(handle);
        if (!service) return fail(error, SE_INVALID_HANDLE, "invalid Redis handle");
        const auto status = service->submit(operation, key, key_size, value, value_size, ttl_ms, *request_id);
        if (status == SE_BACKPRESSURE) return fail(error, status, "Redis request or result budget is full; poll results");
        if (status == SE_STOPPED) return fail(error, status, "Redis service is stopped");
        if (status != SE_OK) return fail(error, status, "invalid Redis key, value, TTL or exhausted request IDs");
        return SE_OK;
    });
}
}

void SE_CALL se_redis_options_init(se_redis_options* options)
{
    if (options == nullptr) return;
    *options = {};
    options->struct_size = sizeof(*options);
    options->abi_version = SE_ABI_VERSION;
    options->port = 6379;
    options->security = SE_SECURITY_TLS;
    options->connect_timeout_ms = 3000;
    options->command_timeout_ms = 3000;
    options->max_inflight_requests = 128;
    options->max_key_bytes = 1024;
    options->max_value_bytes = 1024 * 1024;
    options->max_queue_bytes = 8 * 1024 * 1024;
    options->max_result_bytes = 8 * 1024 * 1024;
}

void SE_CALL se_redis_result_init(se_redis_result* result)
{
    if (result == nullptr) return;
    *result = {};
    result->struct_size = sizeof(*result);
    result->abi_version = SE_ABI_VERSION;
}

se_status SE_CALL se_redis_open(const se_redis_options* options, se_redis_handle* redis, se_error* error)
{
    if (redis != nullptr) *redis = 0;
    return protect(error, [&]() -> se_status {
        if (redis == nullptr) return fail(error, SE_INVALID_ARGUMENT, "Redis handle output is required");
        Options decoded;
        const auto status = decode(options, decoded, error);
        if (status != SE_OK) return status;
        if (!Connection::supported()) return fail(error, SE_NOT_SUPPORTED, "Redis support was not compiled");
        auto service = std::make_shared<Service>(std::move(decoded));
        auto& entries = registry();
        std::lock_guard lock(entries.mutex);
        if (entries.next_id > UINT64_C(0x00FFFFFFFFFFFFFF))
            return fail(error, SE_INVALID_STATE, "Redis handle space exhausted");
        const auto handle = UINT64_C(0x5200000000000000) | entries.next_id;
        entries.services.emplace(handle, std::move(service));
        ++entries.next_id;
        *redis = handle;
        return SE_OK;
    });
}

se_status SE_CALL se_redis_get(se_redis_handle redis, const void* key, std::uint32_t key_size,
    std::uint64_t* request_id, se_error* error)
{
    return submit(redis, SE_REDIS_GET, key, key_size, nullptr, 0, 0, request_id, error);
}

se_status SE_CALL se_redis_set(se_redis_handle redis, const void* key, std::uint32_t key_size,
    const void* value, std::uint32_t value_size, std::uint64_t ttl_ms, std::uint64_t* request_id, se_error* error)
{
    return submit(redis, SE_REDIS_SET, key, key_size, value, value_size, ttl_ms, request_id, error);
}

se_status SE_CALL se_redis_delete(se_redis_handle redis, const void* key, std::uint32_t key_size,
    std::uint64_t* request_id, se_error* error)
{
    return submit(redis, SE_REDIS_DELETE, key, key_size, nullptr, 0, 0, request_id, error);
}

se_status SE_CALL se_redis_poll(se_redis_handle redis, se_redis_result* result, void* value,
    std::uint32_t capacity, std::uint32_t timeout_ms, se_error* error)
{
    return protect(error, [&]() -> se_status {
        if (result == nullptr || result->struct_size != sizeof(*result) || result->abi_version != SE_ABI_VERSION
            || (value == nullptr && capacity != 0))
            return fail(error, SE_INVALID_ARGUMENT, "invalid Redis result layout or payload buffer");
        const auto service = find(redis);
        if (!service) return fail(error, SE_INVALID_HANDLE, "invalid Redis handle");
        return service->poll(*result, value, capacity, timeout_ms);
    });
}

se_status SE_CALL se_redis_stop(se_redis_handle redis, se_error* error)
{
    return protect(error, [&]() -> se_status {
        const auto service = find(redis);
        if (!service) return fail(error, SE_INVALID_HANDLE, "invalid Redis handle");
        service->stop();
        return SE_OK;
    });
}

se_status SE_CALL se_redis_destroy(se_redis_handle redis, se_error* error)
{
    return protect(error, [&]() -> se_status {
        std::shared_ptr<Service> service;
        {
            auto& entries = registry();
            std::lock_guard lock(entries.mutex);
            const auto found = entries.services.find(redis);
            if (found == entries.services.end()) return fail(error, SE_INVALID_HANDLE, "invalid Redis handle");
            service = std::move(found->second);
            entries.services.erase(found);
        }
        service->stop();
        return SE_OK;
    });
}
