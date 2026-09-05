#include "HttpResponseValidation.h"
#include "HttpRequestCodec.h"

#include <boost/beast/core/string.hpp>
#include <algorithm>

namespace serverengine::net::async {
namespace {

bool token_character(unsigned char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') ||
        std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(value)) != std::string_view::npos;
}

bool valid_value(std::string_view value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return byte == '\t' || (byte >= 0x20 && byte != 0x7f);
    });
}

bool engine_header(std::string_view name)
{
    for (const char* reserved : {"Content-Length", "Transfer-Encoding", "Connection",
             "Upgrade", "Trailer", "Keep-Alive", "Proxy-Connection", "TE", "Content-Type"}) {
        if (boost::beast::iequals(boost::beast::string_view(name.data(), name.size()), reserved)) return true;
    }
    return false;
}

} // namespace

bool validate_http_response(const HttpResponse& response, std::size_t max_body_bytes,
    std::size_t& header_bytes, std::string* error)
{
    const auto fail = [error](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (response.status_code < 200 || response.status_code > 599)
        return fail("HTTP response status must be 200..599");
    if (response.body.size() > max_body_bytes)
        return fail("HTTP response body exceeds configured message limit");
    if ((response.status_code == 204 || response.status_code == 205 || response.status_code == 304) &&
        !response.body.empty()) return fail("HTTP 204/205/304 require an empty body");
    if (response.content_type.empty() || response.content_type.size() > 1024 ||
        !valid_value(response.content_type) || response.headers.size() > 64)
        return fail("Invalid HTTP content type or header count");
    header_bytes = 256 + response.content_type.size();
    for (const auto& header : response.headers) {
        if (header.name.empty() || header.name.size() > 128 ||
            !std::all_of(header.name.begin(), header.name.end(), token_character) ||
            header.value.size() > 8192 || !valid_value(header.value) || engine_header(header.name))
            return fail("Invalid HTTP header or attempt to override an engine-owned header");
        header_bytes += header.name.size() + header.value.size() + 4;
        if (header_bytes > http_header_limit)
            return fail("HTTP response headers exceed 16 KiB");
    }
    return true;
}

} // namespace serverengine::net::async
