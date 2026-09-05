#include "GameServer.h"

#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>

namespace {
std::atomic_bool stop_requested{false};
static_assert(std::atomic_bool::is_always_lock_free, "Signal flag must be lock-free");
void request_stop(int) { stop_requested.store(true, std::memory_order_relaxed); }
}

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: GameServer <certificate-chain.pem> <private-key.pem> [game.sqlite]\n"
                     "Clients must trust the certificate and verify its hostname; no bypass is provided.\n";
        return 2;
    }
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    try {
        // Preserve international paths on Windows; both the C ABI and SQLite
        // expect UTF-8, whereas the Windows command line is UTF-16.
        const auto certificate = std::filesystem::path(argv[1]).u8string();
        const auto private_key = std::filesystem::path(argv[2]).u8string();
        const auto database = argc == 4 ? std::filesystem::path(argv[3]).u8string() : "game.sqlite";
        GameServer server(certificate.c_str(), private_key.c_str(), database.c_str());
        std::cout << "GameServer ready. TCP TLS=9443; UDP plaintext=9001; WSS=9444/game. Ctrl+C to stop.\n";
        server.run(stop_requested);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GameServer failed: " << error.what() << '\n';
        return 1;
    }
}
