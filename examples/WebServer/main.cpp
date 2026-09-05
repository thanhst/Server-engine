#include "WebServer.h"
#include <csignal>
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
    if (argc < 3 || argc > 5) {
        std::cerr << "Usage: ServerEngineWebServer <cert.pem> <key.pem> [web.sqlite] [web-root]\n";
        return 2;
    }
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    try {
        const auto certificate = std::filesystem::path(argv[1]).u8string();
        const auto key = std::filesystem::path(argv[2]).u8string();
        const auto database = argc > 3 ? std::filesystem::path(argv[3]).u8string() : "web.sqlite";
        const auto web_root = argc > 4 ? std::filesystem::path(argv[4]) : std::filesystem::path("examples/WebMediaClient");
        WebServer server(certificate.c_str(), key.c_str(), database.c_str(), web_root);
        std::cout << "Open https://localhost:9553/ . WSS signaling:9554/signal; binary TLS TCP:9555.\n"
                     "One unauthenticated lab room, two peers maximum. Ctrl+C to stop.\n";
        server.run(stop_requested);
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "WebServer: " << failure.what() << '\n';
        return 1;
    }
}
