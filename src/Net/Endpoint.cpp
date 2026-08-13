#include <ServerEngine/Net/Endpoint.h>

namespace serverengine::net {

std::string to_string(const Endpoint& endpoint)
{
    return endpoint.address + ":" + std::to_string(endpoint.port);
}

} // namespace serverengine::net
