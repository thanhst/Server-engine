#pragma once

#include "Runtime/Host/ServerHost.h"

#include <memory>

namespace serverengine::abi {

// Numeric tokens prevent a stale C handle from dereferencing freed memory.
// A call retains shared ownership while concurrent destroy removes the token.
std::uint64_t register_host(std::shared_ptr<runtime::host::ServerHost> host);
std::shared_ptr<runtime::host::ServerHost> find_host(std::uint64_t handle);
std::shared_ptr<runtime::host::ServerHost> remove_host(std::uint64_t handle);

} // namespace serverengine::abi
