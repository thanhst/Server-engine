#pragma once

#include <cstdint>
#include <string>

namespace serverengine::net {

struct Endpoint {
    std::string address{"0.0.0.0"};
    std::uint16_t port{};
};

[[nodiscard]] std::string to_string(const Endpoint& endpoint);

} // namespace serverengine::net
