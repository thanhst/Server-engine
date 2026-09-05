#include "ProfileStore.h"
#include <chrono>

namespace {
se_sql_options options_for(const char* file)
{
    if (se_sql_get_abi_version() != SE_SQL_ABI_VERSION) throw std::runtime_error("SQL ABI mismatch");
    se_sql_options options;
    se_sql_options_init(&options);
    options.connection = file;
    options.max_outstanding_requests = 64;
    options.max_request_bytes = 32768;
    options.max_result_bytes = 32768;
    options.memory_budget_bytes = 8 * 1024 * 1024;
    return options;
}
}

ProfileStore::ProfileStore(const char* file) : sql_(options_for(file))
{
    // Startup migration is intentionally awaited BEFORE the network starts.
    // Normal web requests below always submit/poll without blocking the loop.
    constexpr char create[] = "CREATE TABLE IF NOT EXISTS sample_profiles (id INTEGER PRIMARY KEY,name TEXT NOT NULL,level INTEGER NOT NULL)";
    constexpr char seed[] = "INSERT INTO sample_profiles(id,name,level) VALUES(1,'Demo player',1) ON CONFLICT(id) DO NOTHING";
    const se_sql_statement statements[] = {
        {create, sizeof(create) - 1, 0, nullptr}, {seed, sizeof(seed) - 1, 0, nullptr}
    };
    const auto request = sql_.submit(statements, 2);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    serverengine::sdk::SqlResult result;
    while (sql_.poll(result, 100) == SE_TIMEOUT) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Profile migration timed out");
    }
    if (result.metadata.request_id != request || result.metadata.status != SE_OK)
        throw std::runtime_error("Profile migration failed");
}

std::uint64_t ProfileStore::load_sample()
{
    se_sql_parameter id{};
    id.type = SE_SQL_INT64;
    id.integer = 1;
    return sql_.query("SELECT id,name,level FROM sample_profiles WHERE id=?", &id, 1);
}

se_status ProfileStore::poll(serverengine::sdk::SqlResult& result) { return sql_.poll(result); }
