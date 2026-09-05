#pragma once

#include <ServerEngine/Core/Buffer.h>

#include <cstdint>
#include <string>
#include <vector>

namespace serverengine::net {

struct HttpHeader { std::string name; std::string value; };

// The public C facade copies inputs into this owned value before posting an
// HTTP response to the transport worker. No caller pointer crosses threads.
struct HttpResponse {
    std::uint32_t status_code{200};
    bool close_connection{false};
    std::string content_type{"application/octet-stream"};
    std::vector<HttpHeader> headers;
    core::Buffer body;
};

} // namespace serverengine::net
