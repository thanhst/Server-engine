#include <ServerEngine/C/DatagramTransport.h>
#include <boost/asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Udp = boost::asio::ip::udp;
using Clock = std::chrono::steady_clock;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

// Real UDP proxy: impair only this test's packets, never vendor global state.
// One held packet bounds memory. Every operation is nonblocking so join is bounded.
class ImpairedPath {
public:
    explicit ImpairedPath(unsigned short server_port)
        : socket_(io_, Udp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)),
          server_(boost::asio::ip::make_address("127.0.0.1"), server_port)
    {
        port_ = socket_.local_endpoint().port();
        socket_.non_blocking(true);
        worker_ = std::thread([this] { run(); });
    }
    ~ImpairedPath() { stop(); }
    unsigned short port() const { return port_; }
    void stop() { stopping_ = true; if (worker_.joinable()) worker_.join(); }
    void verify() {
        stop();
        check(error_.empty(), error_.c_str());
        check(dropped_ && duplicated_ && reordered_, "Proxy did not exercise all impairments");
        std::cout << "Dropped=" << dropped_ << " duplicated=" << duplicated_
                  << " reordered=" << reordered_ << '\n';
    }
private:
    void forward(const void* data, std::size_t size, const Udp::endpoint& destination) {
        boost::system::error_code error;
        socket_.send_to(boost::asio::buffer(data, size), destination, 0, error);
        if (error && error != boost::asio::error::would_block && error != boost::asio::error::try_again)
            throw boost::system::system_error(error);
    }
    void run() noexcept {
        try {
            std::array<unsigned char, 65536> bytes{};
            std::vector<unsigned char> held;
            Udp::endpoint held_destination;
            auto held_at = Clock::now();
            std::uint64_t packet = 0;
            while (!stopping_) {
                Udp::endpoint sender;
                boost::system::error_code error;
                const auto size = socket_.receive_from(boost::asio::buffer(bytes), sender, 0, error);
                if (error == boost::asio::error::would_block || error == boost::asio::error::try_again) {
                    if (!held.empty() && Clock::now() - held_at > std::chrono::milliseconds(3)) {
                        forward(held.data(), held.size(), held_destination);
                        held.clear();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                if (error) throw boost::system::system_error(error);
                if (sender != server_) client_ = sender;
                if (client_.port() == 0) continue;
                const auto destination = sender == server_ ? client_ : server_;
                ++packet;
                if (packet % 11 == 0) { ++dropped_; continue; }
                if (packet % 13 == 0 && held.empty()) {
                    held.assign(bytes.begin(), bytes.begin() + size);
                    held_destination = destination;
                    held_at = Clock::now();
                    continue;
                }
                forward(bytes.data(), size, destination);
                if (packet % 17 == 0) { forward(bytes.data(), size, destination); ++duplicated_; }
                if (!held.empty()) {
                    forward(held.data(), held.size(), held_destination);
                    held.clear();
                    ++reordered_;
                }
            }
        } catch (const std::exception& error) { error_ = error.what(); }
        catch (...) { error_ = "Unknown proxy failure"; }
    }
    boost::asio::io_context io_;
    Udp::socket socket_;
    Udp::endpoint server_, client_;
    unsigned short port_{};
    std::atomic_bool stopping_{false};
    std::thread worker_;
    // Read only after stop/join.
    std::string error_;
    unsigned dropped_{}, duplicated_{}, reordered_{};
};

struct Endpoint {
    se_datagram_handle value{};
    ~Endpoint() { if (value) se_datagram_destroy(value, nullptr); }
};

std::array<unsigned char, 2048> packet(unsigned sequence) {
    std::array<unsigned char, 2048> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<unsigned char>((i + sequence) % 251);
    result[0] = static_cast<unsigned char>(sequence >> 24);
    result[1] = static_cast<unsigned char>(sequence >> 16);
    result[2] = static_cast<unsigned char>(sequence >> 8);
    result[3] = static_cast<unsigned char>(sequence);
    return result;
}
} // namespace

int main() {
    try {
        check(se_datagram_get_abi_version() == SE_DATAGRAM_ABI_VERSION, "ABI mismatch");
        se_datagram_options options;
        se_datagram_options_init(&options);
        options.max_peers = 2;
        options.max_message_bytes = 4096;
        options.max_send_queue_bytes = 65536;
        Endpoint server, client;
        const auto status = se_datagram_create(&options, &server.value, nullptr);
        if (status == SE_NOT_SUPPORTED) return 77;
        check(status == SE_OK, "Server create failed");
        check(se_datagram_create(&options, &client.value, nullptr) == SE_OK, "Client create failed");
        unsigned short port = 0;
        const auto seed = static_cast<unsigned>(Clock::now().time_since_epoch().count()) % 16000;
        for (unsigned attempt = 0; attempt < 100 && !port; ++attempt) {
            const auto candidate = static_cast<unsigned short>(20000 + (seed + attempt) % 16000);
            if (se_datagram_listen(server.value, "127.0.0.1", candidate, nullptr) == SE_OK) port = candidate;
        }
        check(port != 0, "Could not bind a server port");
        ImpairedPath path(port);
        std::uint64_t client_peer = 0, server_peer = 0;
        check(se_datagram_connect(client.value, "127.0.0.1", path.port(), &client_peer, nullptr) == SE_OK,
            "Connect initiation failed");
        bool connected = false, received_done = false, sent_done = false;
        unsigned sent = 0, received = 0;
        constexpr unsigned count = 128;
        const auto deadline = Clock::now() + std::chrono::seconds(35);
        while (!received_done && Clock::now() < deadline) {
            for (const auto current : {server.value, client.value}) {
                for (unsigned burst = 0; burst < 64; ++burst) {
                    se_datagram_event event;
                    se_datagram_event_init(&event);
                    std::array<unsigned char, 4096> payload{};
                    const auto polled = se_datagram_poll(current, &event, payload.data(),
                        static_cast<std::uint32_t>(payload.size()), 0, nullptr);
                    if (polled == SE_TIMEOUT) break;
                    check(polled == SE_OK, "Poll failed under packet loss");
                    check(event.kind != SE_DATAGRAM_OVERFLOW && event.kind != SE_DATAGRAM_DISCONNECTED,
                        "Unexpected overflow/disconnect under packet loss");
                    if (event.kind == SE_DATAGRAM_CONNECTED) {
                        if (current == client.value) connected = true;
                        else server_peer = event.peer_id;
                    } else if (event.kind == SE_DATAGRAM_MESSAGE) {
                        check(event.delivery == SE_DATAGRAM_RELIABLE_ORDERED, "Reliable flag lost");
                        if (current == server.value) {
                            const auto expected = packet(received);
                            check(received < count && event.payload_size == expected.size() &&
                                std::memcmp(payload.data(), expected.data(), expected.size()) == 0,
                                "Reliable message lost, duplicated, corrupted or out of order");
                            ++received;
                        } else {
                            check(event.payload_size == 4 && std::memcmp(payload.data(), "DONE", 4) == 0,
                                "Unexpected server response");
                            received_done = true;
                        }
                    }
                }
            }
            if (connected && sent < count) {
                const auto message = packet(sent);
                const auto outcome = se_datagram_send(client.value, client_peer, SE_DATAGRAM_RELIABLE_ORDERED,
                    message.data(), static_cast<std::uint32_t>(message.size()), nullptr);
                check(outcome == SE_OK || outcome == SE_BACKPRESSURE, "Reliable send failed");
                if (outcome == SE_OK) ++sent;
            }
            if (received == count && !sent_done) {
                const auto outcome = se_datagram_send(server.value, server_peer,
                    SE_DATAGRAM_RELIABLE_ORDERED, "DONE", 4, nullptr);
                check(outcome == SE_OK || outcome == SE_BACKPRESSURE, "Reply failed");
                sent_done = outcome == SE_OK;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(received_done && sent == count && received == count, "Reliable delivery deadline exceeded");
        path.verify();
        std::cout << "Reliable messages survived real UDP loss, duplication and reordering\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
