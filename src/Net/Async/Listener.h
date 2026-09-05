#pragma once

namespace serverengine::net::async {

class Listener {
public:
    virtual ~Listener() = default;
    virtual void start() = 0;
    virtual void close() = 0;
};

} // namespace serverengine::net::async
