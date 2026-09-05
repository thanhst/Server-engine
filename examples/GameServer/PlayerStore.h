#pragma once

#include <ServerEngine/Cpp/Engine.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

// The application owns its schema/SQL. The DLL owns connections and execution.
// Startup waits for migrations; every operation during gameplay is asynchronous.
class PlayerStore final {
public:
    enum class Operation { Open, Save, Close, History };
    struct Completion {
        std::uint64_t request_id{}, session{};
        Operation operation{};
        se_status status{};
        std::string message;
        std::int64_t stored_messages{};
    };

    explicit PlayerStore(const char* utf8_file);
    void open_connection(std::uint64_t session, std::uint32_t protocol, std::string_view address);
    void save_message_count(std::uint64_t session, std::int64_t count);
    void close_connection(std::uint64_t session, std::int64_t count);
    void load_history(std::uint64_t session);
    bool poll(Completion& completion, std::uint32_t timeout_ms = 0);
    void stop() { sql_.stop(); }
    std::size_t pending_count() const { return pending_.size(); }

private:
    struct Pending { std::uint64_t session; Operation operation; };
    void submit(std::uint64_t session, Operation operation, std::string_view sql,
        const se_sql_parameter* parameters, std::uint32_t count);
    void update(std::uint64_t session, std::int64_t count, bool closed);

    serverengine::sdk::Sql sql_;
    std::int64_t run_id_{}; // DB-generated run ID keeps session IDs unique across restarts.
    std::unordered_map<std::uint64_t, Pending> pending_;
};
