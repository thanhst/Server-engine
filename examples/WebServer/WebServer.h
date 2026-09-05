#pragma once
#include "ProfileStore.h"
#include "SignalRoom.h"
#include <ServerEngine/C/Http.h>
#include <atomic>
#include <filesystem>
#include <unordered_map>

class WebServer final {
public:
    WebServer(const char* certificate, const char* key, const char* database,
        const std::filesystem::path& web_root);
    void run(const std::atomic_bool& stop);
private:
    struct PendingProfile { std::uint64_t session, http_request; };
    void handle(const serverengine::sdk::NetworkEvent& event);
    void route(const se_event& event, std::string_view payload);
    void poll_profiles();
    void respond(std::uint64_t session, std::uint64_t request, std::uint32_t status,
        const char* content_type, std::string_view body);
    bool send(std::uint64_t session, std::string_view body);
    ProfileStore profiles_;
    serverengine::sdk::Network network_;
    SignalRoom room_;
    std::string html_, javascript_;
    std::unordered_map<std::uint64_t, PendingProfile> pending_;
};
