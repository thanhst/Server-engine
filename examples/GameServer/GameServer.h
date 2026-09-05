#pragma once

#include <ServerEngine/C/ServerEngine.h>
#include "PlayerStore.h"

#include <atomic>
#include <string>
#include <string_view>
#include <unordered_map>

// Network and SQL execution are in the DLL. Game commands/schema stay here.
class GameServer final {
public:
    GameServer(const char* certificate, const char* private_key, const char* database);
    ~GameServer();
    void run(const std::atomic_bool& stop_requested);
    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

private:
    struct EngineHandle {
        se_server_handle value{};
        ~EngineHandle() { if (value) se_server_destroy(value, nullptr); }
    };
    struct PlayerSession {
        std::string display_name{"Guest"}; // A label, not login/authentication.
        std::int64_t messages{};
        bool logging_requested{}; // Async INSERT accepted; completion confirms whether it committed.
    };
    void add_listener(std::uint32_t protocol, std::uint32_t port,
        const char* certificate, const char* private_key);
    void handle_event(const se_event& event, std::string_view payload);
    void handle_message(const se_event& event, std::string_view payload);
    void poll_database();
    void reply(std::uint64_t session, std::string_view text);

    PlayerStore store_;
    EngineHandle engine_;
    std::unordered_map<std::uint64_t, PlayerSession> players_;
};
