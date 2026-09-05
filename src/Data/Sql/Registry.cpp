#include "Service.h"

#include <limits>
#include <unordered_map>

namespace serverengine::data::sql {
namespace {
struct Registry {
    std::mutex mutex;
    std::uint64_t next_handle{UINT64_C(0x5300000000000001)};
    std::unordered_map<std::uint64_t, std::shared_ptr<Service>> services;
};

Registry& registry()
{
    static Registry state;
    return state;
}
} // namespace

std::uint64_t register_service(std::shared_ptr<Service> service)
{
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.next_handle == UINT64_C(0x5400000000000000))
        throw SqlError(SE_INVALID_STATE, "SQL service handle space exhausted");
    const auto handle = state.next_handle++;
    state.services.emplace(handle, std::move(service));
    return handle;
}

std::shared_ptr<Service> find_service(std::uint64_t handle)
{
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto found = state.services.find(handle);
    if (found == state.services.end()) throw SqlError(SE_INVALID_HANDLE, "unknown or destroyed SQL service");
    return found->second;
}

std::shared_ptr<Service> remove_service(std::uint64_t handle)
{
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto found = state.services.find(handle);
    if (found == state.services.end()) throw SqlError(SE_INVALID_HANDLE, "unknown or destroyed SQL service");
    auto service = std::move(found->second);
    state.services.erase(found);
    return service;
}

} // namespace serverengine::data::sql
