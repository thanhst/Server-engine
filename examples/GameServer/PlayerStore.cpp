#include "PlayerStore.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace {
se_sql_options database_options(const char* filename)
{
    if (se_sql_get_abi_version() != SE_SQL_ABI_VERSION) throw std::runtime_error("SQL ABI mismatch");
    se_sql_options options;
    se_sql_options_init(&options);
    options.connection = filename;
    options.max_outstanding_requests = 512;
    options.max_result_bytes = 64 * 1024;
    return options;
}

se_sql_parameter integer(std::int64_t value)
{
    se_sql_parameter parameter{};
    parameter.type = SE_SQL_INT64;
    parameter.integer = value;
    return parameter;
}

se_sql_parameter text(std::string_view value)
{
    se_sql_parameter parameter{};
    parameter.type = SE_SQL_TEXT;
    parameter.data = value.data();
    parameter.size = static_cast<std::uint32_t>(value.size());
    return parameter;
}

std::int64_t database_session(std::uint64_t session)
{
    if (session > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
        throw std::overflow_error("Session ID exceeds SQL integer range");
    return static_cast<std::int64_t>(session);
}
} // namespace

PlayerStore::PlayerStore(const char* utf8_file) : sql_(database_options(utf8_file))
{
    // Migrations stay in the game. The final INSERT allocates this engine run's
    // identity; legacy example table `connections` is deliberately untouched.
    const std::string_view migrations[] = {
        "CREATE TABLE IF NOT EXISTS engine_runs (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "opened_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')))",
        "CREATE TABLE IF NOT EXISTS engine_connections (run_id INTEGER NOT NULL, session_id INTEGER NOT NULL, "
        "protocol INTEGER NOT NULL, peer_address TEXT NOT NULL, messages INTEGER NOT NULL DEFAULT 0, "
        "opened_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')), closed_utc TEXT, "
        "PRIMARY KEY(run_id,session_id))",
        "INSERT INTO engine_runs DEFAULT VALUES"
    };
    se_sql_statement statements[3]{};
    for (std::uint32_t i = 0; i < 3; ++i)
        statements[i] = {migrations[i].data(), static_cast<std::uint32_t>(migrations[i].size()), 0, nullptr};
    const auto request = sql_.submit(statements, 3);
    serverengine::sdk::SqlResult result;
    if (sql_.poll(result, 10000) != SE_OK || result.metadata.request_id != request)
        throw std::runtime_error("Database startup migration did not complete");
    if (result.metadata.status != SE_OK) throw std::runtime_error(result.metadata.message);
    run_id_ = result.metadata.last_insert_id;
}

void PlayerStore::submit(std::uint64_t session, Operation operation, std::string_view query,
    const se_sql_parameter* parameters, std::uint32_t count)
{
    // Allocate tracking before admitting a write. Rekeying this node keeps the
    // same map size, so publishing its real request ID needs no new allocation.
    const auto slot = pending_.emplace(0, Pending{session, operation});
    if (!slot.second) throw std::logic_error("SQL submit cannot reenter application tracking");
    std::uint64_t request{};
    try { request = sql_.query(query, parameters, count); }
    catch (...) { pending_.erase(slot.first); throw; }
    auto node = pending_.extract(slot.first);
    node.key() = request;
    pending_.insert(std::move(node));
}

void PlayerStore::open_connection(std::uint64_t session, std::uint32_t protocol, std::string_view address)
{
    const se_sql_parameter parameters[]{integer(run_id_), integer(database_session(session)), integer(protocol), text(address)};
    submit(session, Operation::Open,
        "INSERT INTO engine_connections(run_id,session_id,protocol,peer_address) VALUES(?,?,?,?)", parameters, 4);
}

void PlayerStore::update(std::uint64_t session, std::int64_t count, bool closed)
{
    const se_sql_parameter parameters[]{integer(count), integer(run_id_), integer(database_session(session))};
    submit(session, closed ? Operation::Close : Operation::Save,
        closed ? "UPDATE engine_connections SET messages=?,closed_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE run_id=? AND session_id=?"
               : "UPDATE engine_connections SET messages=? WHERE run_id=? AND session_id=?", parameters, 3);
}

void PlayerStore::save_message_count(std::uint64_t session, std::int64_t count) { update(session, count, false); }
void PlayerStore::close_connection(std::uint64_t session, std::int64_t count) { update(session, count, true); }

void PlayerStore::load_history(std::uint64_t session)
{
    const se_sql_parameter parameters[]{integer(run_id_), integer(database_session(session))};
    submit(session, Operation::History,
        "SELECT messages FROM engine_connections WHERE run_id=? AND session_id=?", parameters, 2);
}

bool PlayerStore::poll(Completion& completion, std::uint32_t timeout_ms)
{
    serverengine::sdk::SqlResult result; // Destruction releases the DLL result and its reservation.
    if (sql_.poll(result, timeout_ms) != SE_OK) return false;
    const auto found = pending_.find(result.metadata.request_id);
    if (found == pending_.end()) throw std::runtime_error("Database completion has no matching application request");
    completion = {};
    completion.request_id = result.metadata.request_id;
    completion.session = found->second.session;
    completion.operation = found->second.operation;
    completion.status = result.metadata.status;
    completion.message = result.metadata.message;
    pending_.erase(found);
    if (completion.status == SE_OK && completion.operation == Operation::History) {
        if (result.metadata.row_count != 1) {
            completion.status = SE_IO_ERROR;
            completion.message = "No stored session found";
        } else {
            completion.stored_messages = result.integer(0, 0);
        }
    }
    return true;
}
