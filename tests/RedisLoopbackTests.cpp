#include <ServerEngine/C/Redis.h>

#include <boost/asio.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace asio = boost::asio;
using Tcp = asio::ip::tcp;
namespace {
void require(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}

std::string command(std::initializer_list<std::string> arguments)
{
    std::string bytes = "*" + std::to_string(arguments.size()) + "\r\n";
    for (const auto& argument : arguments)
        bytes += "$" + std::to_string(argument.size()) + "\r\n" + argument + "\r\n";
    return bytes;
}

struct Exchange { std::string command; std::string reply; };

// Scripted RESP peer, not a real Redis service. The script checks the exact
// binary-safe wire command. Empty reply deliberately simulates a stalled peer.
class Peer {
public:
    explicit Peer(std::vector<Exchange> exchanges)
        : acceptor_(io_, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)), socket_(io_),
          timer_(io_), exchanges_(std::move(exchanges)), first_request_(received_.get_future())
    {
        timer_.expires_after(std::chrono::seconds(10));
        timer_.async_wait([this](boost::system::error_code error) {
            if (!error) { failed_ = true; finish(); }
        });
        acceptor_.async_accept(socket_, [this](boost::system::error_code error) {
            if (error) { failed_ = true; finish(); return; }
            read();
        });
        worker_ = std::thread([this] { io_.run(); });
    }
    ~Peer()
    {
        asio::post(io_, [this] { finish(); });
        if (worker_.joinable()) worker_.join();
    }
    std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
    bool received_request() { return first_request_.wait_for(std::chrono::seconds(3)) == std::future_status::ready; }
    void verify()
    {
        if (worker_.joinable()) worker_.join();
        require(!failed_ && index_ == exchanges_.size(), "scripted Redis wire exchange");
    }
private:
    void finish()
    {
        boost::system::error_code ignored;
        try { (void)timer_.cancel(); } catch (...) { failed_ = true; }
        acceptor_.close(ignored);
        socket_.close(ignored);
    }
    void read()
    {
        if (index_ == exchanges_.size()) { finish(); return; }
        incoming_.resize(exchanges_[index_].command.size());
        asio::async_read(socket_, asio::buffer(incoming_),
            [this](boost::system::error_code error, std::size_t) {
                if (error || incoming_ != exchanges_[index_].command) { failed_ = true; finish(); return; }
                if (index_ == 0) received_.set_value();
                if (exchanges_[index_].reply.empty()) return;
                asio::async_write(socket_, asio::buffer(exchanges_[index_].reply),
                    [this](boost::system::error_code write_error, std::size_t) {
                        if (write_error) { failed_ = true; finish(); return; }
                        ++index_;
                        read();
                    });
            });
    }
    asio::io_context io_;
    Tcp::acceptor acceptor_;
    Tcp::socket socket_;
    asio::steady_timer timer_;
    std::vector<Exchange> exchanges_;
    std::size_t index_ = 0;
    std::string incoming_;
    std::promise<void> received_;
    std::future<void> first_request_;
    bool failed_ = false;
    std::thread worker_;
};

struct Client {
    se_redis_handle handle = 0;
    se_error error{};
    ~Client() { if (handle != 0) se_redis_destroy(handle, nullptr); }
    se_status open(std::uint16_t port, std::uint32_t timeout = 1000)
    {
        se_redis_options options;
        se_redis_options_init(&options);
        options.port = port;
        options.security = SE_SECURITY_NONE;
        options.command_timeout_ms = timeout;
        options.max_inflight_requests = 2;
        options.max_key_bytes = 16;
        options.max_value_bytes = 64;
        options.max_queue_bytes = 128;
        options.max_result_bytes = 64; // Exactly one outstanding GET reservation.
        return se_redis_open(&options, &handle, &error);
    }
    se_redis_result poll(void* bytes = nullptr, std::uint32_t capacity = 0)
    {
        se_redis_result result;
        se_redis_result_init(&result);
        require(se_redis_poll(handle, &result, bytes, capacity, 3000, &error) == SE_OK, "Redis completion");
        return result;
    }
};

void binary_values_and_admission()
{
    const std::string key("k\0y", 3);
    const std::string value("a\0\xffz", 4);
    Peer peer({
        {command({"GET", key}), "$4\r\n" + value + "\r\n"},
        {command({"SET", key, value, "PX", "1500"}), "+OK\r\n"},
        {command({"DEL", key}), ":1\r\n"},
        {command({"GET", "empty"}), "$0\r\n\r\n"},
        {command({"GET", "missing"}), "$-1\r\n"}
    });
    Client client;
    require(client.open(peer.port()) == SE_OK, "Redis open");
    std::uint64_t request = 0;
    require(se_redis_get(client.handle, key.data(), 3, &request, &client.error) == SE_OK, "GET accepted");
    const auto first = request;
    require(se_redis_get(client.handle, key.data(), 3, &request, &client.error) == SE_BACKPRESSURE && request == 0,
        "GET reserves result budget until consumed");
    se_redis_result result;
    se_redis_result_init(&result);
    require(se_redis_poll(client.handle, &result, nullptr, 0, 3000, &client.error) == SE_BUFFER_TOO_SMALL
        && result.request_id == first && result.value_size == 4 && result.found == 1, "size probe retains result");
    char buffer[64]{};
    result = client.poll(buffer, sizeof(buffer));
    require(result.request_id == first && result.status == SE_OK && std::string(buffer, 4) == value, "binary GET");
    require(se_redis_set(client.handle, key.data(), 3, value.data(), 4, 1500, &request, &client.error) == SE_OK,
        "SET accepted");
    result = client.poll();
    require(result.status == SE_OK && result.affected_count == 1, "SET TTL complete");
    require(se_redis_delete(client.handle, key.data(), 3, &request, &client.error) == SE_OK, "DELETE accepted");
    result = client.poll();
    require(result.status == SE_OK && result.affected_count == 1, "DELETE complete");
    require(se_redis_get(client.handle, "empty", 5, &request, &client.error) == SE_OK, "empty GET accepted");
    result = client.poll();
    require(result.status == SE_OK && result.found == 1 && result.value_size == 0, "empty value is found");
    require(se_redis_get(client.handle, "missing", 7, &request, &client.error) == SE_OK, "missing GET accepted");
    result = client.poll();
    require(result.status == SE_OK && result.found == 0, "missing key distinguished");
    peer.verify();
}

void timeout_has_unknown_write_outcome()
{
    Peer peer({{command({"SET", "key", "value"}), ""}});
    Client client;
    require(client.open(peer.port(), 150) == SE_OK, "Redis timeout open");
    std::uint64_t request = 0;
    require(se_redis_set(client.handle, "key", 3, "value", 5, 0, &request, &client.error) == SE_OK, "SET accepted");
    require(peer.received_request(), "write reached peer");
    const auto result = client.poll();
    require(result.status == SE_OUTCOME_UNKNOWN, "timed-out write must not claim rollback");
}

void oversized_value_is_not_returned()
{
    Peer peer({{command({"GET", "large"}), "$65\r\n" + std::string(65, 'x') + "\r\n"}});
    Client client;
    require(client.open(peer.port()) == SE_OK, "Redis limit open");
    std::uint64_t request = 0;
    require(se_redis_get(client.handle, "large", 5, &request, &client.error) == SE_OK, "large GET accepted");
    const auto result = client.poll();
    require(result.status == SE_RESULT_TOO_LARGE && result.value_size == 0, "oversized value rejected");
    peer.verify();
}

void oversized_advertised_length_is_rejected_before_body()
{
    // No bulk body follows. A length-only peer must neither trigger a huge
    // parser allocation nor hold the connection until the normal deadline.
    Peer peer({{command({"GET", "large"}), "$4294967296\r\n"}});
    Client client;
    require(client.open(peer.port(), 30000) == SE_OK, "Redis advertised limit open");
    std::uint64_t request = 0;
    require(se_redis_get(client.handle, "large", 5, &request, &client.error) == SE_OK, "large header GET accepted");
    const auto result = client.poll();
    require(result.status == SE_RESULT_TOO_LARGE, "advertised length checked before RESP parser");
    peer.verify();
}

void stop_cancels_read_and_preserves_queued_completion()
{
    Peer peer({{command({"GET", "key"}), ""}});
    Client client;
    require(client.open(peer.port(), 30000) == SE_OK, "Redis stop open");
    std::uint64_t request = 0;
    require(se_redis_get(client.handle, "key", 3, &request, &client.error) == SE_OK, "GET accepted");
    require(peer.received_request(), "read waiting on peer");
    require(se_redis_set(client.handle, "key", 3, "v", 1, 0, &request, &client.error) == SE_OK, "queued SET");
    const auto queued = request;
    require(se_redis_delete(client.handle, "key", 3, &request, &client.error) == SE_BACKPRESSURE && request == 0,
        "queued plus running requests exhaust admission slots");
    const auto start = std::chrono::steady_clock::now();
    require(se_redis_stop(client.handle, &client.error) == SE_OK, "stop cancels pending socket");
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(3), "stop does not wait command timeout");
    auto result = client.poll();
    require(result.operation == SE_REDIS_GET && result.status == SE_STOPPED, "interrupted GET completion");
    result = client.poll();
    require(result.request_id == queued && result.status == SE_STOPPED, "queued SET was never sent");
}
}

int main()
{
    try {
        require(se_get_abi_version() == SE_ABI_VERSION, "ABI version");
        Client probe;
        if (probe.open(6379) == SE_NOT_SUPPORTED) {
            std::cout << "SKIP Redis loopback: connector disabled\n";
            return 77;
        }
        require(probe.handle != 0, "Redis capability probe");
        binary_values_and_admission();
        timeout_has_unknown_write_outcome();
        oversized_value_is_not_returned();
        oversized_advertised_length_is_rejected_before_body();
        stop_cancels_read_and_preserves_queued_completion();
        std::cout << "PASS Redis binary protocol, budgets, timeout and stop\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
