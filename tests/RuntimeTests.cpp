#include "Runtime/SessionRegistry.h"

#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using serverengine::net::Endpoint;
using serverengine::net::TransportKind;
using serverengine::runtime::ConnectionHub;
using serverengine::runtime::detail::SessionRegistry;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void listeners_share_unique_session_ids()
{
    ConnectionHub hub;
    SessionRegistry registry(hub, 2);

    // Listener-local connection IDs can both be 1; the registry issues IDs
    // independently of either transport's counter.
    const auto first = registry.try_add(TransportKind::Tcp, Endpoint{"127.0.0.1", 41001});
    const auto second = registry.try_add(TransportKind::Tcp, Endpoint{"127.0.0.1", 41002});
    require(first && second && *first != *second, "listeners must receive distinct session IDs");
    require(hub.count() == 2, "registering another listener must not overwrite a session");

    require(hub.authenticate(*first, "alice"), "first session must be present");
    require(!hub.is_authenticated(*second), "authentication must not leak between listeners");
    registry.remove(*first);
    require(hub.find(*second).has_value(), "disconnect must not remove another listener's session");

    const auto replacement = registry.try_add(TransportKind::Tcp, Endpoint{"127.0.0.1", 41003});
    require(replacement && *replacement != *first && *replacement != *second,
        "released session IDs must not be reused");
}

void session_limit_releases_capacity_after_disconnect()
{
    ConnectionHub hub;
    SessionRegistry registry(hub, 1);
    const Endpoint endpoint{"127.0.0.1", 42001};

    const auto first = registry.try_add(TransportKind::Tcp, endpoint);
    require(first.has_value(), "first session should be admitted");
    require(!registry.try_add(TransportKind::Tcp, endpoint), "a full runtime must reject admission");
    registry.remove(*first);
    require(registry.try_add(TransportKind::Tcp, endpoint).has_value(), "disconnect must release capacity");

    ConnectionHub closed_hub;
    SessionRegistry closed_registry(closed_hub, 0);
    require(!closed_registry.try_add(TransportKind::Tcp, endpoint), "zero capacity must reject admission");
}

void concurrent_listeners_cannot_exceed_the_global_limit()
{
    constexpr std::size_t max_sessions = 37;
    constexpr std::size_t listener_count = 12;
    ConnectionHub hub;
    SessionRegistry registry(hub, max_sessions);
    std::atomic_size_t admitted{0};
    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::vector<std::thread> listeners;

    for (std::size_t listener = 0; listener < listener_count; ++listener) {
        listeners.emplace_back([&registry, &admitted, ready, listener] {
            ready.wait();
            const Endpoint endpoint{"127.0.0.1", static_cast<std::uint16_t>(43000 + listener)};
            for (std::size_t attempt = 0; attempt < 100; ++attempt) {
                if (registry.try_add(TransportKind::Tcp, endpoint)) {
                    admitted.fetch_add(1);
                }
            }
        });
    }

    start.set_value();
    for (auto& listener : listeners) {
        listener.join();
    }

    require(admitted.load() == max_sessions, "concurrent admission must enforce the exact global limit");
    require(hub.count() == max_sessions, "all admitted sessions must have unique hub entries");
}

} // namespace

int main()
{
    try {
        listeners_share_unique_session_ids();
        session_limit_releases_capacity_after_disconnect();
        concurrent_listeners_cannot_exceed_the_global_limit();
        std::cout << "Runtime tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Runtime test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
