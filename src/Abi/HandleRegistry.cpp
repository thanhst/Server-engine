#include "HandleRegistry.h"

#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace serverengine::abi {
namespace {

struct Registry {
    std::mutex mutex;
    std::uint64_t next_handle{1};
    std::unordered_map<std::uint64_t, std::shared_ptr<runtime::host::ServerHost>> hosts;
};

Registry& registry()
{
    static Registry instance;
    return instance;
}

} // namespace

std::uint64_t register_host(std::shared_ptr<runtime::host::ServerHost> host)
{
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    // Upper byte identifies the service family: network=0, SQL='S', Redis='R'.
    if (state.next_handle > UINT64_C(0x00ffffffffffffff)) {
        throw std::runtime_error("server handle space exhausted");
    }
    const auto id = state.next_handle++;
    state.hosts.emplace(id, std::move(host));
    return id;
}

std::shared_ptr<runtime::host::ServerHost> find_host(std::uint64_t handle)
{
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto found = state.hosts.find(handle);
    return found == state.hosts.end() ? nullptr : found->second;
}

std::shared_ptr<runtime::host::ServerHost> remove_host(std::uint64_t handle)
{
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto found = state.hosts.find(handle);
    if (found == state.hosts.end()) {
        return nullptr;
    }
    auto host = std::move(found->second);
    state.hosts.erase(found);
    return host; // stop/join happens after the registry lock is released.
}

} // namespace serverengine::abi
