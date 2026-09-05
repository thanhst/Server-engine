#include <ServerEngine/C/Sql.h>

#include <array>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Database {
    se_sql_handle handle{};
    explicit Database(std::uint32_t outstanding = 8, std::uint32_t result_bytes = 8192)
    {
        se_sql_options options;
        se_sql_options_init(&options);
        options.connection = ":memory:";
        options.max_outstanding_requests = outstanding;
        options.max_result_bytes = result_bytes;
        se_error error{};
        require(se_sql_create(&options, &handle, &error) == SE_OK, "create SQLite service");
    }
    ~Database() { if (handle) se_sql_destroy(handle, nullptr); }

    std::uint64_t submit(const char* text, const se_sql_parameter* parameters = nullptr, std::uint32_t count = 0)
    {
        se_sql_statement statement{text, static_cast<std::uint32_t>(std::strlen(text)), count, parameters};
        std::uint64_t id{};
        se_error error{};
        require(se_sql_submit(handle, &statement, 1, &id, &error) == SE_OK && id != 0, "submit SQL");
        return id;
    }
    se_sql_result poll()
    {
        se_sql_result result;
        se_sql_result_init(&result);
        require(se_sql_poll(handle, &result, 5000, nullptr) == SE_OK, "poll SQL completion");
        return result;
    }
    void release(const se_sql_result& result)
    {
        require(se_sql_release_result(handle, result.request_id, nullptr) == SE_OK, "release SQL result");
    }
    void execute(const char* sql)
    {
        const auto id = submit(sql);
        auto result = poll();
        require(result.request_id == id && result.status == SE_OK, "execute SQL statement");
        release(result);
    }
};

void typed_results_and_input_copy()
{
    Database database;
    const char sample[] = u8"người chơi\0suffix";
    std::string text(sample, sizeof(sample) - 1);
    const std::string original = text;
    std::array<unsigned char, 3> blob{{0, 0xff, 7}};
    std::array<se_sql_parameter, 6> parameters{};
    parameters[0].type = SE_SQL_INT64;
    parameters[0].integer = INT64_C(9223372036854775806);
    parameters[1].type = SE_SQL_DOUBLE;
    parameters[1].real = 1.25;
    parameters[2].type = SE_SQL_TEXT;
    parameters[2].data = text.data();
    parameters[2].size = static_cast<std::uint32_t>(text.size());
    parameters[3].type = SE_SQL_BLOB;
    parameters[3].data = blob.data();
    parameters[3].size = static_cast<std::uint32_t>(blob.size());
    parameters[4].type = SE_SQL_NULL;
    parameters[5].type = SE_SQL_BLOB; // Empty BLOB must not become SQL NULL.
    const auto id = database.submit("SELECT ? AS integer_value, ?, ?, ?, ?, ?", parameters.data(), 6);
    text.assign(text.size(), 'x');
    blob.fill(1);
    auto result = database.poll();
    require(result.request_id == id && result.status == SE_OK && result.row_count == 1 && result.column_count == 6,
        "typed result metadata");
    se_sql_cell cell{};
    require(se_sql_get_cell(database.handle, id, 0, 0, &cell, nullptr, 0, nullptr) == SE_OK
        && cell.type == SE_SQL_INT64 && cell.integer == parameters[0].integer, "int64 round trip");
    require(se_sql_get_cell(database.handle, id, 0, 1, &cell, nullptr, 0, nullptr) == SE_OK
        && cell.type == SE_SQL_DOUBLE && cell.real == 1.25, "double round trip");
    require(se_sql_get_cell(database.handle, id, 0, 2, &cell, nullptr, 0, nullptr) == SE_BUFFER_TOO_SMALL
        && cell.type == SE_SQL_TEXT && cell.size == original.size(), "text size probe retains result");
    std::vector<char> output(cell.size);
    require(se_sql_get_cell(database.handle, id, 0, 2, &cell, output.data(), static_cast<std::uint32_t>(output.size()), nullptr) == SE_OK
        && std::string(output.begin(), output.end()) == original, "copied UTF-8 and embedded NUL survive caller mutation");
    std::array<unsigned char, 3> copied{};
    require(se_sql_get_cell(database.handle, id, 0, 3, &cell, copied.data(), 3, nullptr) == SE_OK
        && copied[0] == 0 && copied[1] == 0xff && copied[2] == 7, "copied binary parameter");
    require(se_sql_get_cell(database.handle, id, 0, 4, &cell, nullptr, 0, nullptr) == SE_OK
        && cell.type == SE_SQL_NULL, "NULL value");
    require(se_sql_get_cell(database.handle, id, 0, 5, &cell, nullptr, 0, nullptr) == SE_OK
        && cell.type == SE_SQL_BLOB && cell.size == 0, "empty BLOB differs from NULL");
    std::uint32_t name_size{};
    require(se_sql_column_name(database.handle, id, 0, nullptr, 0, &name_size, nullptr) == SE_BUFFER_TOO_SMALL,
        "column name size probe");
    std::vector<char> name(name_size + 1);
    require(se_sql_column_name(database.handle, id, 0, name.data(), static_cast<std::uint32_t>(name.size()), &name_size, nullptr) == SE_OK
        && std::string(name.data()) == "integer_value", "column name retry terminates");
    database.release(result);
    require(se_sql_get_cell(database.handle, id, 0, 0, &cell, nullptr, 0, nullptr) == SE_INVALID_HANDLE,
        "released result cannot be read");
}

void atomic_batch_and_forbidden_control()
{
    Database database;
    database.execute("CREATE TABLE items(id INTEGER PRIMARY KEY, name TEXT)");
    const char* first = "INSERT INTO items VALUES(1, 'first')";
    const char* second = "INSERT INTO items VALUES(1, 'duplicate')";
    se_sql_statement batch[]{{first, static_cast<std::uint32_t>(std::strlen(first)), 0, nullptr},
        {second, static_cast<std::uint32_t>(std::strlen(second)), 0, nullptr}};
    std::uint64_t id{};
    require(se_sql_submit(database.handle, batch, 2, &id, nullptr) == SE_OK, "submit atomic batch");
    auto failed = database.poll();
    require(failed.status != SE_OK && failed.affected_rows == 0, "constraint failure rolls back batch");
    database.release(failed);
    database.submit("SELECT count(*) FROM items");
    auto count = database.poll();
    se_sql_cell cell{};
    require(se_sql_get_cell(database.handle, count.request_id, 0, 0, &cell, nullptr, 0, nullptr) == SE_OK
        && cell.integer == 0, "first batch write was rolled back");
    database.release(count);
    for (const char* denied : {"BEGIN", "COMMIT", "SAVEPOINT hidden", "PRAGMA foreign_keys=OFF",
             "ATTACH ':memory:' AS hidden", "SELECT 1; INSERT INTO items VALUES(2,'hidden')"}) {
        database.submit(denied);
        auto result = database.poll();
        require(result.status != SE_OK, "connection state/control SQL rejected");
        database.release(result);
    }
    database.execute("INSERT INTO items VALUES(3,'valid after rejected control')");
}

void completion_capacity_and_stop()
{
    Database database(1);
    database.execute("CREATE TABLE writes(id INTEGER)");
    const auto id = database.submit("INSERT INTO writes VALUES(1)");
    auto committed = database.poll();
    require(committed.request_id == id && committed.status == SE_OK, "write completion preserved");
    const char* text = "SELECT count(*) FROM writes";
    se_sql_statement statement{text, static_cast<std::uint32_t>(std::strlen(text)), 0, nullptr};
    std::uint64_t rejected = 123;
    require(se_sql_submit(database.handle, &statement, 1, &rejected, nullptr) == SE_BACKPRESSURE && rejected == 0,
        "delivered unreleased result still reserves completion capacity");
    database.release(committed);
    database.submit(text);
    auto count = database.poll();
    se_sql_cell cell{};
    require(se_sql_get_cell(database.handle, count.request_id, 0, 0, &cell, nullptr, 0, nullptr) == SE_OK
        && cell.integer == 1, "backpressured request did not duplicate writes");
    database.release(count);

    auto waiter = std::async(std::launch::async, [&] {
        se_sql_result result;
        se_sql_result_init(&result);
        return se_sql_poll(database.handle, &result, 30000, nullptr);
    });
    require(se_sql_stop(database.handle, nullptr) == SE_OK, "stop service");
    require(waiter.get() == SE_STOPPED, "stop wakes blocked poller");
    require(se_sql_stop(database.handle, nullptr) == SE_OK, "stop idempotent");
    require(se_sql_submit(database.handle, &statement, 1, &rejected, nullptr) == SE_STOPPED,
        "stopped service rejects new work");
}

void bounded_results_and_cancellation()
{
    Database database(8, 1024);
    database.execute("CREATE TABLE changes(id INTEGER)");
    const char* write = "INSERT INTO changes VALUES(1)";
    const char* too_large = "SELECT zeroblob(2000)";
    se_sql_statement batch[]{{write, static_cast<std::uint32_t>(std::strlen(write)), 0, nullptr},
        {too_large, static_cast<std::uint32_t>(std::strlen(too_large)), 0, nullptr}};
    std::uint64_t id{};
    require(se_sql_submit(database.handle, batch, 2, &id, nullptr) == SE_OK, "submit oversized result batch");
    auto result = database.poll();
    require(result.status == SE_RESULT_TOO_LARGE, "oversized result has explicit failure");
    database.release(result);
    database.submit("SELECT count(*) FROM changes");
    result = database.poll();
    se_sql_cell cell{};
    require(se_sql_get_cell(database.handle, result.request_id, 0, 0, &cell, nullptr, 0, nullptr) == SE_OK
        && cell.integer == 0, "result limit rolls back earlier batch writes");
    database.release(result);

    const auto slow = database.submit("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<1000000000) SELECT sum(x) FROM n");
    const auto queued = database.submit("INSERT INTO changes VALUES(2)");
    require(se_sql_stop(database.handle, nullptr) == SE_OK, "stop interrupts active work");
    auto first = database.poll();
    auto second = database.poll();
    require(first.request_id == slow && first.status == SE_SQL_CANCELLED, "active or queued long query cancelled");
    require(second.request_id == queued && second.status == SE_SQL_CANCELLED, "queued write cancellation retained");
    database.release(first);
    database.release(second);
}

void options_and_stale_handles()
{
    se_sql_options options;
    se_sql_options_init(&options);
    options.connection = ":memory:";
    options.provider = SE_SQL_MYSQL;
    se_sql_handle service = 123;
    require(se_sql_create(&options, &service, nullptr) == SE_NOT_SUPPORTED && service == 0,
        "unavailable provider explicitly rejected");
    options.provider = SE_SQL_SQLITE;
    options.reserved[0] = 1;
    require(se_sql_create(&options, &service, nullptr) == SE_INVALID_ARGUMENT, "unknown reserved option rejected");
    options.reserved[0] = 0;
    require(se_sql_create(&options, &service, nullptr) == SE_OK, "create handle");
    const auto stale = service;
    require(se_sql_destroy(service, nullptr) == SE_OK, "destroy handle");
    require(se_sql_create(&options, &service, nullptr) == SE_OK && service != stale, "SQL handle never reused");
    require(se_sql_stop(stale, nullptr) == SE_INVALID_HANDLE, "stale service cannot stop replacement");
    require(se_sql_destroy(service, nullptr) == SE_OK, "destroy replacement");
}
} // namespace

int main()
{
    try {
        require(se_sql_get_abi_version() == SE_SQL_ABI_VERSION, "SQL ABI version");
        typed_results_and_input_copy();
        atomic_batch_and_forbidden_control();
        completion_capacity_and_stop();
        bounded_results_and_cancellation();
        options_and_stale_handles();
        std::cout << "PASS SQL C ABI, transactions, bounded completion, cancellation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL SQL: " << error.what() << '\n';
        return 1;
    }
}
