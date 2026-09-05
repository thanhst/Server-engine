#include <ServerEngine/C/Http.h>

#include "Boundary.h"
#include "HandleRegistry.h"
#include "Net/Async/HttpRequestCodec.h"
#include "Net/Async/HttpResponseValidation.h"

#include <iterator>
#include <utility>

using namespace serverengine;

namespace {

bool copy_text(const char* source, std::size_t maximum, std::string& output)
{
    if (!source) return false;
    std::size_t size{};
    while (size <= maximum && source[size] != '\0') ++size;
    if (size > maximum) return false;
    output.assign(source, size);
    return true;
}

bool decode_response(const se_http_response* source, std::size_t body_limit,
    net::HttpResponse& response, std::string& error)
{
    if (!source || source->struct_size != sizeof(*source) || source->abi_version != SE_ABI_VERSION ||
        !std::all_of(std::begin(source->reserved), std::end(source->reserved), [](auto n) { return n == 0; }) ||
        source->close_connection > 1 || source->header_count > 64 ||
        (source->header_count != 0 && !source->headers) ||
        (source->body_size != 0 && !source->body) || source->body_size > body_limit) {
        error = "Invalid HTTP response size/version/reserved, pointer, or body/header limit";
        return false;
    }
    response.status_code = source->status_code;
    response.close_connection = source->close_connection != 0;
    if (source->content_type && !copy_text(source->content_type, 1024, response.content_type)) {
        error = "HTTP content type exceeds 1024 bytes";
        return false;
    }
    std::size_t header_bytes = 256 + response.content_type.size();
    for (std::uint32_t i = 0; i < source->header_count; ++i) {
        net::HttpHeader header;
        if (!copy_text(source->headers[i].name, 128, header.name) ||
            !copy_text(source->headers[i].value, 8192, header.value)) {
            error = "Invalid HTTP header string or length";
            return false;
        }
        header_bytes += header.name.size() + header.value.size() + 4;
        if (header_bytes > net::async::http_header_limit) {
            error = "HTTP response headers exceed 16 KiB";
            return false;
        }
        response.headers.push_back(std::move(header));
    }
    response.body = core::Buffer(static_cast<const core::Buffer::Byte*>(source->body), source->body_size);
    return net::async::validate_http_response(response, body_limit, header_bytes, &error);
}

} // namespace

se_status SE_CALL se_http_request_read(const void* payload, uint32_t size,
    se_http_request* request, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        net::async::HttpRequestView view;
        if (!request || !net::async::decode_http_request(payload, size, view))
            return abi::fail(error, SE_INVALID_ARGUMENT, "Invalid HTTP request event payload");
        *request = {};
        request->struct_size = sizeof(*request);
        request->abi_version = SE_ABI_VERSION;
        request->request_id = view.request_id;
        request->method_offset = view.method_offset;
        request->method_size = view.method_size;
        request->target_offset = view.target_offset;
        request->target_size = view.target_size;
        request->headers_offset = view.headers_offset;
        request->headers_size = view.headers_size;
        request->body_offset = view.body_offset;
        request->body_size = view.body_size;
        return SE_OK;
    });
}

void SE_CALL se_http_response_init(se_http_response* response)
{
    if (!response) return;
    *response = {};
    response->struct_size = sizeof(*response);
    response->abi_version = SE_ABI_VERSION;
    response->status_code = 200;
}

se_status SE_CALL se_http_respond(se_server_handle server, uint64_t session_id,
    uint64_t request_id, const se_http_response* response, se_error* error)
{
    return abi::protect(error, [&]() -> se_status {
        if (session_id == 0 || request_id == 0)
            return abi::fail(error, SE_INVALID_ARGUMENT, "HTTP session and request IDs must be nonzero");
        const auto host = abi::find_host(server);
        if (!host) return abi::fail(error, SE_INVALID_HANDLE, "Unknown server handle");
        if (host->overflowed())
            return abi::fail(error, SE_BACKPRESSURE, "Event queue overflowed; recreate server");
        net::HttpResponse decoded;
        std::string detail;
        if (!decode_response(response, host->max_message_bytes(), decoded, detail))
            return abi::fail(error, SE_INVALID_ARGUMENT, detail);
        return host->respond_http(session_id, request_id, decoded, detail)
            ? SE_OK : abi::fail(error, SE_IO_ERROR, detail);
    });
}
