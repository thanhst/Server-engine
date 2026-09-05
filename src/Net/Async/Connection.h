#pragma once

#include <ServerEngine/Net/TransportService.h>

namespace serverengine::net::async {

// Methods run on the single transport worker. Emergency cleanup can also close
// resources after that worker has joined. No socket or queue needs its own mutex.
class Connection {
public:
    virtual ~Connection() = default;
    [[nodiscard]] virtual const Peer& peer() const noexcept = 0;
    virtual void start() = 0;
    [[nodiscard]] virtual bool send(const core::Buffer&, std::string* error) = 0;
    [[nodiscard]] virtual bool respond_http(std::uint64_t, const HttpResponse&, std::string* error)
    {
        if (error) *error = "Session does not use HTTP";
        return false;
    }
    virtual void close() = 0;
};

} // namespace serverengine::net::async
