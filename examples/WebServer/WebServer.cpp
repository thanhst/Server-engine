#include "WebServer.h"
#include <fstream>
#include <iostream>

namespace {
se_server_options network_options()
{
    if (se_get_abi_version() != SE_ABI_VERSION) throw std::runtime_error("Network ABI mismatch");
    se_server_options options;
    se_server_options_init(&options);
    options.max_connections = 128;
    options.max_message_bytes = 128 * 1024;
    options.max_send_queue_bytes = 512 * 1024;
    options.max_event_queue_count = 512;
    options.idle_timeout_ms = 60000;
    return options;
}
std::string read_asset(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open web assets; run from repository root or pass web-root");
    const auto length = file.tellg();
    if (length < 0 || length > 65536) throw std::runtime_error("Web asset exceeds 64 KiB");
    std::string bytes(static_cast<std::size_t>(length), '\0');
    file.seekg(0);
    if (!bytes.empty() && !file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read web asset");
    return bytes;
}
std::string json_string(std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string output = "\"";
    for (unsigned char byte : value) {
        if (byte == '"' || byte == '\\') { output += '\\'; output += static_cast<char>(byte); }
        else if (byte < 32) { output += "\\u00"; output += hex[byte >> 4]; output += hex[byte & 15]; }
        else output += static_cast<char>(byte);
    }
    return output + '"';
}
}

WebServer::WebServer(const char* certificate, const char* key, const char* database,
    const std::filesystem::path& web_root)
    : profiles_(database), network_(network_options()),
      room_([this](auto session, auto text) { return send(session, text); },
            [this](auto session) { se_server_disconnect(network_.handle(), session, nullptr); }),
      html_(read_asset(web_root / "index.html")), javascript_(read_asset(web_root / "app.js"))
{
    for (const auto protocol : {SE_PROTOCOL_HTTP, SE_PROTOCOL_WEBSOCKET, SE_PROTOCOL_TCP}) {
        se_listener_options options;
        se_listener_options_init(&options);
        options.protocol = protocol;
        options.port = protocol == SE_PROTOCOL_HTTP ? 9553 : protocol == SE_PROTOCOL_WEBSOCKET ? 9554 : 9555;
        options.certificate_chain_file = certificate;
        options.private_key_file = key;
        options.websocket_path = "/signal";
        network_.listen(options); // Numeric loopback bind and TLS by default.
    }
    network_.start();
}

void WebServer::run(const std::atomic_bool& stop)
{
    serverengine::sdk::NetworkEvent event;
    while (!stop.load(std::memory_order_relaxed)) {
        // Service both queues each iteration. One busy network cannot starve SQL.
        const auto status = network_.poll(event, 5);
        if (status == SE_STOPPED) break;
        if (status == SE_OK) handle(event);
        poll_profiles();
    }
    network_.stop();
}

void WebServer::handle(const serverengine::sdk::NetworkEvent& event)
{
    const auto& meta = event.metadata;
    std::cout << "event=" << meta.sequence << " listener=" << meta.listener_id
              << " session=" << meta.session_id << " kind=" << meta.kind << '\n';
    if (meta.kind == SE_EVENT_OVERFLOW) throw std::runtime_error("Network event overflow; restart the example");
    if (meta.kind == SE_EVENT_ERROR) { std::cerr << event.bytes() << '\n'; return; }
    if (meta.kind == SE_EVENT_HTTP_REQUEST) { route(meta, event.bytes()); return; }
    if (meta.protocol == SE_PROTOCOL_WEBSOCKET) {
        if (meta.kind == SE_EVENT_OPEN) room_.opened(meta.session_id);
        if (meta.kind == SE_EVENT_MESSAGE) room_.message(meta.session_id, event.bytes());
        if (meta.kind == SE_EVENT_CLOSE) room_.closed(meta.session_id);
    } else if (meta.protocol == SE_PROTOCOL_TCP && meta.kind == SE_EVENT_MESSAGE) {
        send(meta.session_id, event.bytes() == "PING" ? std::string_view("PONG") : event.bytes());
    }
    if (meta.kind == SE_EVENT_CLOSE) {
        for (auto item = pending_.begin(); item != pending_.end();) {
            if (item->second.session == meta.session_id) item = pending_.erase(item);
            else ++item;
        }
    }
}

void WebServer::route(const se_event& event, std::string_view payload)
{
    se_http_request request{};
    se_error error{};
    serverengine::sdk::require(se_http_request_read(payload.data(), static_cast<std::uint32_t>(payload.size()), &request, &error), error);
    const auto method = payload.substr(request.method_offset, request.method_size);
    const auto target = payload.substr(request.target_offset, request.target_size);
    const auto session = event.session_id, id = request.request_id;
    if (method == "POST" && target == "/api/echo") {
        respond(session, id, 200, "application/octet-stream", payload.substr(request.body_offset, request.body_size));
    } else if (method != "GET" && method != "HEAD") {
        respond(session, id, 405, "text/plain; charset=utf-8", "Method not allowed");
    } else if (target == "/") {
        respond(session, id, 200, "text/html; charset=utf-8", html_);
    } else if (target == "/app.js") {
        respond(session, id, 200, "text/javascript; charset=utf-8", javascript_);
    } else if (target == "/health") {
        respond(session, id, 200, "application/json", "{\"network\":\"ready\"}");
    } else if (target == "/api/profile") {
        // Allocate application bookkeeping before accepting a database job.
        // Key zero is reserved; node re-keying after submit allocates nothing.
        try {
            pending_.emplace(0, PendingProfile{session, id});
            std::uint64_t query{};
            try { query = profiles_.load_sample(); }
            catch (...) { pending_.erase(0); throw; }
            auto node = pending_.extract(0);
            node.key() = query;
            pending_.insert(std::move(node));
            std::cout << "http_request=" << id << " sql_request=" << query << " submitted\n";
        } catch (const std::exception&) { respond(session, id, 503, "text/plain", "Database temporarily unavailable"); }
    } else respond(session, id, 404, "text/plain", "Not found");
}

void WebServer::poll_profiles()
{
    serverengine::sdk::SqlResult result;
    for (unsigned count = 0; count < 16 && profiles_.poll(result) == SE_OK; ++count) {
        const auto found = pending_.find(result.metadata.request_id);
        if (found == pending_.end()) continue; // Disconnected peer; RAII still releases the SQL result.
        const auto pending = found->second;
        pending_.erase(found);
        std::cout << "http_request=" << pending.http_request << " sql_request=" << result.metadata.request_id
                  << " completed status=" << result.metadata.status << '\n';
        if (result.metadata.status != SE_OK) {
            respond(pending.session, pending.http_request, 500, "text/plain", "Profile query failed");
        } else if (result.metadata.row_count == 0) {
            respond(pending.session, pending.http_request, 404, "text/plain", "Profile not found");
        } else {
            const auto body = "{\"id\":" + std::to_string(result.integer(0, 0)) +
                ",\"name\":" + json_string(result.text(0, 1)) + ",\"level\":" + std::to_string(result.integer(0, 2)) + "}";
            respond(pending.session, pending.http_request, 200, "application/json; charset=utf-8", body);
        }
    }
}

void WebServer::respond(std::uint64_t session, std::uint64_t request, std::uint32_t status,
    const char* content_type, std::string_view body)
{
    const se_http_header headers[] = {{"Cache-Control", "no-store"},
        {"X-Content-Type-Options", "nosniff"}, {"Allow", "GET, HEAD, POST"}};
    se_http_response response;
    se_http_response_init(&response);
    response.status_code = status;
    response.content_type = content_type;
    response.headers = headers;
    response.header_count = status == 405 ? 3 : 2;
    response.body = body.data();
    response.body_size = static_cast<std::uint32_t>(body.size());
    se_error error{};
    const auto outcome = se_http_respond(network_.handle(), session, request, &response, &error);
    if (outcome != SE_OK) {
        // Timed-out requests can have queued events/results. They must not be
        // used to answer a later request on the same keepalive connection.
        std::cerr << "http_request=" << request << " response rejected: " << error.message << '\n';
        se_server_disconnect(network_.handle(), session, nullptr);
    }
}

bool WebServer::send(std::uint64_t session, std::string_view body)
{
    se_error error{};
    const auto status = se_server_send(network_.handle(), session, body.data(), static_cast<std::uint32_t>(body.size()), &error);
    if (status != SE_OK) {
        std::cerr << "session=" << session << " send failed: " << error.message << '\n';
        se_server_disconnect(network_.handle(), session, nullptr);
    }
    return status == SE_OK;
}
