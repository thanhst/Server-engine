#pragma once

#include "CompletionPort.h"

#if SERVERENGINE_OS_WINDOWS
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace serverengine::net::iocp {

class Connection;

enum class OperationKind { Receive, Send };

// Deriving from OVERLAPPED lets the worker recover the owner with static_cast.
// A submitted operation owns its buffers and keeps its Connection alive until
// the completion packet is consumed, including cancellation during shutdown.
struct Operation final : OVERLAPPED {
    Operation(CompletionPort& port, OperationKind kind, std::shared_ptr<Connection> owner)
        : OVERLAPPED{}, kind(kind), connection(std::move(owner)), port_(port)
    {
        port_.operation_started();
    }

    ~Operation() { port_.operation_finished(); }

    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;

    [[nodiscard]] CompletionPort& port() noexcept { return port_; }

    OperationKind kind;
    std::shared_ptr<Connection> connection;
    WSABUF buffer{};
    std::array<char, 8192> receive_buffer{};
    std::vector<char> send_buffer;
    std::size_t send_offset{0};

private:
    CompletionPort& port_;
};

} // namespace serverengine::net::iocp
#endif
