#pragma once
#include <ServerEngine/Cpp/Engine.h>

// The application owns table names and SQL. The DLL owns execution and results.
class ProfileStore final {
public:
    explicit ProfileStore(const char* utf8_database);
    std::uint64_t load_sample();
    se_status poll(serverengine::sdk::SqlResult& result);
private:
    serverengine::sdk::Sql sql_;
};
