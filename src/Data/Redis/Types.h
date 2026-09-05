#pragma once

#include <ServerEngine/C/Redis.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace serverengine::data::redis {

using Bytes = std::vector<std::uint8_t>;

struct Options {
    std::string address = "127.0.0.1";
    std::string username;
    std::string password;
    std::uint16_t port = 6379;
    std::uint32_t connect_timeout_ms = 3000;
    std::uint32_t command_timeout_ms = 3000;
    std::uint32_t max_inflight_requests = 128;
    std::uint32_t max_key_bytes = 1024;
    std::uint32_t max_value_bytes = 1024 * 1024;
    std::uint64_t max_queue_bytes = 8 * 1024 * 1024;
    std::uint64_t max_result_bytes = 8 * 1024 * 1024;
};

struct Completion {
    std::uint64_t request_id = 0;
    std::uint32_t operation = 0;
    se_status status = SE_OK;
    bool found = false;
    std::uint64_t affected_count = 0;
    Bytes value;
    std::array<char, 192> message{};
    void fail(se_status code, const char* description) noexcept;
};

struct Request {
    Bytes key;
    Bytes value;
    std::uint64_t ttl_ms = 0;
    std::uint64_t result_reservation = 0;
    Completion completion;
};

} // namespace serverengine::data::redis
