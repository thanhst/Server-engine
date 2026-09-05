#include <ServerEngine/Cpp/Engine.h>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
}
int main()
{
    using namespace serverengine::sdk;
    SqlResult retained;
    bool rejected = false;
    try { (void)retained.integer(0, 0); } catch (const std::logic_error&) { rejected = true; }
    check(rejected, "Empty result must fail without dereferencing a null owner");
    {
        check(se_sql_get_abi_version() == SE_SQL_ABI_VERSION, "SQL ABI version");
        se_sql_options options;
        se_sql_options_init(&options);
        options.connection = ":memory:";
        Sql sql(options);
        const auto id = sql.query("SELECT 42, 'binary-safe text'");
        check(sql.poll(retained, 5000) == SE_OK && retained.metadata.request_id == id, "Result completion");
    }
    // SqlResult holds shared service ownership, not an untracked vendor pointer.
    check(retained.integer(0, 0) == 42 && retained.text(0, 1) == "binary-safe text", "Result must survive Sql wrapper destruction");
    SqlResult moved(std::move(retained));
    check(moved.integer(0, 0) == 42, "Moving a result must preserve its owner");
    rejected = false;
    try { (void)retained.text(0, 0); } catch (const std::logic_error&) { rejected = true; }
    check(rejected, "Moved-from result must fail safely");
    moved.reset(); // Releases result first, then stops and destroys its service.
    std::cout << "C++ SDK ownership tests passed\n";
}
