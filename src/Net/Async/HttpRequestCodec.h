#pragma once

#include <ServerEngine/Core/Buffer.h>

namespace serverengine::net::async {

// This event envelope is private to the DLL. C callers use the checked decoder
// in Http.h, never pointer casts. Explicit byte encoding avoids alignment and
// C/C++ struct-padding assumptions.
struct HttpRequestView {
    std::uint64_t request_id{};
    std::uint32_t method_offset{}, method_size{};
    std::uint32_t target_offset{}, target_size{};
    std::uint32_t headers_offset{}, headers_size{};
    std::uint32_t body_offset{}, body_size{};
};

constexpr std::size_t http_envelope_bytes = 32;
constexpr std::size_t http_header_limit = 16 * 1024;

core::Buffer encode_http_request(std::uint64_t id, std::string_view method,
    std::string_view target, std::string_view headers, const std::vector<std::uint8_t>& body);
bool decode_http_request(const void* payload, std::size_t size, HttpRequestView& output) noexcept;

} // namespace serverengine::net::async
