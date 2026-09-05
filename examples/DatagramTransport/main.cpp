#include <ServerEngine/Cpp/DatagramTransport.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {
using Clock = std::chrono::steady_clock;
using serverengine::sdk::DatagramDelivery;
using serverengine::sdk::DatagramEndpoint;
using serverengine::sdk::DatagramEvent;
std::atomic_bool stop_requested{false};
static_assert(std::atomic_bool::is_always_lock_free, "Signal flag must be lock-free");
void request_stop(int) { stop_requested.store(true, std::memory_order_relaxed); }

bool snapshot_number(std::string_view message, std::uint64_t& sequence)
{
    constexpr std::string_view prefix = "SNAP ";
    if (message.substr(0, prefix.size()) != prefix) return false;
    message.remove_prefix(prefix.size());
    const auto parsed = std::from_chars(message.data(), message.data() + message.size(), sequence);
    return parsed.ec == std::errc{} && parsed.ptr == message.data() + message.size();
}

void server(std::uint32_t port)
{
    auto options = DatagramEndpoint::default_options();
    options.max_message_bytes = 4096;
    DatagramEndpoint endpoint(options);
    endpoint.listen(port);
    std::unordered_map<std::uint64_t, std::uint64_t> snapshots;
    std::cout << "Datagram transport server: 127.0.0.1:" << port << ". Ctrl+C to stop.\n";
    while (!stop_requested.load(std::memory_order_relaxed)) {
        DatagramEvent event;
        const auto status = endpoint.poll(event, 20);
        if (status == SE_TIMEOUT) continue;
        if (status == SE_STOPPED) break;
        const auto& meta = event.metadata;
        if (meta.kind == SE_DATAGRAM_CONNECTED) {
            snapshots.emplace(meta.peer_id, 0);
            std::cout << "CONNECTED peer=" << meta.peer_id << '\n';
        } else if (meta.kind == SE_DATAGRAM_DISCONNECTED) {
            snapshots.erase(meta.peer_id);
            std::cout << "DISCONNECTED peer=" << meta.peer_id << " reason=" << meta.reason << '\n';
        } else if (meta.kind == SE_DATAGRAM_OVERFLOW) {
            throw std::runtime_error("Datagram event overflow; recreate endpoint");
        } else if (meta.kind == SE_DATAGRAM_MESSAGE) {
            if (meta.delivery == SE_DATAGRAM_RELIABLE_ORDERED && event.bytes() == "PING") {
                if (endpoint.send(meta.peer_id, DatagramDelivery::ReliableOrdered, "PONG") != SE_OK) {
                    std::cerr << "PONG queue full; disconnect slow peer=" << meta.peer_id << '\n';
                    endpoint.disconnect(meta.peer_id);
                }
            } else if (meta.delivery == SE_DATAGRAM_UNRELIABLE) {
                std::uint64_t sequence{};
                auto found = snapshots.find(meta.peer_id);
                if (found != snapshots.end() && snapshot_number(event.bytes(), sequence) && sequence > found->second) {
                    // Application snapshot sequence discards old/duplicate state.
                    // Unreliable transport itself promises no ordering or delivery.
                    found->second = sequence;
                    std::cout << "SNAP peer=" << meta.peer_id << " sequence=" << sequence << '\n';
                    endpoint.send(meta.peer_id, DatagramDelivery::Unreliable, event.bytes());
                }
            }
        }
    }
}

void client(std::uint32_t port)
{
    DatagramEndpoint endpoint;
    const auto peer = endpoint.connect(port);
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    auto next_snapshot = Clock::now();
    bool connected = false, ping_queued = false, pong_received = false;
    std::uint64_t next_sequence = 1, newest_snapshot = 0;
    std::cout << "Connecting to 127.0.0.1:" << port << " (10-second demonstration)...\n";
    while (!stop_requested.load(std::memory_order_relaxed) && Clock::now() < deadline) {
        DatagramEvent event;
        const auto status = endpoint.poll(event, 10);
        if (status == SE_STOPPED) break;
        if (status == SE_OK) {
            if (event.metadata.kind == SE_DATAGRAM_CONNECTED) connected = true;
            else if (event.metadata.kind == SE_DATAGRAM_DISCONNECTED)
                throw std::runtime_error("Peer disconnected: " + std::string(event.metadata.message));
            else if (event.metadata.kind == SE_DATAGRAM_OVERFLOW)
                throw std::runtime_error("Datagram event overflow; recreate endpoint");
            else if (event.metadata.kind == SE_DATAGRAM_MESSAGE) {
                if (event.metadata.delivery == SE_DATAGRAM_RELIABLE_ORDERED && event.bytes() == "PONG") {
                    pong_received = true;
                    std::cout << "Reliable reply: PONG\n";
                }
                std::uint64_t sequence{};
                if (event.metadata.delivery == SE_DATAGRAM_UNRELIABLE && snapshot_number(event.bytes(), sequence)
                    && sequence > newest_snapshot) {
                    newest_snapshot = sequence;
                    std::cout << "Unreliable snapshot echo: " << sequence << '\n';
                }
            }
        }
        if (!connected) continue;
        if (!ping_queued)
            ping_queued = endpoint.send(peer, DatagramDelivery::ReliableOrdered, "PING") == SE_OK;
        if (next_sequence <= 20 && Clock::now() >= next_snapshot) {
            endpoint.send(peer, DatagramDelivery::Unreliable, "SNAP " + std::to_string(next_sequence++));
            next_snapshot = Clock::now() + std::chrono::milliseconds(50);
        }
        if (pong_received && next_sequence > 20 && newest_snapshot != 0) break;
    }
    if (stop_requested.load(std::memory_order_relaxed)) return;
    if (!pong_received) throw std::runtime_error("No reliable PONG before demonstration deadline");
    std::cout << "Finished: PONG acknowledged application handling; newest snapshot=" << newest_snapshot
              << ". Missing snapshots are allowed.\n";
    endpoint.disconnect(peer); // Example work is done; destroy may cancel other queued messages.
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3 || (std::string_view(argv[1]) != "--server" && std::string_view(argv[1]) != "--client")) {
        std::cerr << "Usage: ServerEngineDatagramTransportExample --server PORT | --client PORT\n"
                     "Run the two roles in separate terminals. Loopback only; protocol differs from raw UDP.\n";
        return 2;
    }
    std::uint32_t port{};
    const std::string_view port_text(argv[2]);
    const auto parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() + port_text.size() || port == 0 || port > 65535) {
        std::cerr << "Port must be 1..65535\n";
        return 2;
    }
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    try {
        if (std::string_view(argv[1]) == "--server") server(port); else client(port);
        return 0;
    } catch (const serverengine::sdk::DatagramTransportError& error) {
        if (error.status() == SE_NOT_SUPPORTED)
            std::cerr << "Datagram transport is disabled; build the DLL with SE_WITH_GNS=ON.\n";
        else std::cerr << "Datagram transport: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Datagram example: " << error.what() << '\n';
        return 1;
    }
}
