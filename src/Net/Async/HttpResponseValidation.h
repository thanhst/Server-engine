#pragma once

#include <ServerEngine/Net/HttpTypes.h>

namespace serverengine::net::async {

// Returns a conservative bound for status line + generated/custom headers.
// Keeping this independent of sockets also protects callers of the C++ service.
bool validate_http_response(const HttpResponse& response, std::size_t max_body_bytes,
    std::size_t& header_bytes, std::string* error);

} // namespace serverengine::net::async
