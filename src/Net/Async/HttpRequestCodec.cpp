#include "HttpRequestCodec.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace serverengine::net::async {
namespace {

void write_u32(std::uint8_t* bytes, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept
{
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes[i]) << (8 * i);
    return value;
}

} // namespace

core::Buffer encode_http_request(std::uint64_t id, std::string_view method,
    std::string_view target, std::string_view headers, const std::vector<std::uint8_t>& body)
{
    const std::uint64_t total = http_envelope_bytes + std::uint64_t(method.size()) +
        target.size() + headers.size() + body.size();
    if (total > (std::numeric_limits<std::uint32_t>::max)())
        throw std::length_error("HTTP event size exceeds encoding range");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(total));
    bytes[0] = 'S'; bytes[1] = 'E'; bytes[2] = 'H'; bytes[3] = 1;
    write_u32(bytes.data() + 4, static_cast<std::uint32_t>(total));
    write_u32(bytes.data() + 8, static_cast<std::uint32_t>(id));
    write_u32(bytes.data() + 12, static_cast<std::uint32_t>(id >> 32));
    write_u32(bytes.data() + 16, static_cast<std::uint32_t>(method.size()));
    write_u32(bytes.data() + 20, static_cast<std::uint32_t>(target.size()));
    write_u32(bytes.data() + 24, static_cast<std::uint32_t>(headers.size()));
    write_u32(bytes.data() + 28, static_cast<std::uint32_t>(body.size()));
    auto cursor = bytes.begin() + http_envelope_bytes;
    cursor = std::copy(method.begin(), method.end(), cursor);
    cursor = std::copy(target.begin(), target.end(), cursor);
    cursor = std::copy(headers.begin(), headers.end(), cursor);
    std::copy(body.begin(), body.end(), cursor);
    return core::Buffer(std::move(bytes));
}

bool decode_http_request(const void* payload, std::size_t size, HttpRequestView& output) noexcept
{
    if (!payload || size < http_envelope_bytes || size > UINT32_MAX) return false;
    const auto* bytes = static_cast<const std::uint8_t*>(payload);
    if (bytes[0] != 'S' || bytes[1] != 'E' || bytes[2] != 'H' || bytes[3] != 1 ||
        read_u32(bytes + 4) != size) return false;
    HttpRequestView result;
    result.request_id = read_u32(bytes + 8) | (std::uint64_t(read_u32(bytes + 12)) << 32);
    result.method_size = read_u32(bytes + 16);
    result.target_size = read_u32(bytes + 20);
    result.headers_size = read_u32(bytes + 24);
    result.body_size = read_u32(bytes + 28);
    const auto total = std::uint64_t(http_envelope_bytes) + result.method_size +
        result.target_size + result.headers_size + result.body_size;
    if (!result.request_id || total != size || result.method_size == 0 || result.method_size > 32 ||
        result.target_size == 0 || result.target_size > 2048 ||
        result.headers_size > http_header_limit) return false;
    result.method_offset = static_cast<std::uint32_t>(http_envelope_bytes);
    result.target_offset = result.method_offset + result.method_size;
    result.headers_offset = result.target_offset + result.target_size;
    result.body_offset = result.headers_offset + result.headers_size;
    output = result;
    return true;
}

} // namespace serverengine::net::async
