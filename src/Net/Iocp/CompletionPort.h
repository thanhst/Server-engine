#pragma once

#include "WinSockSupport.h"

#if SERVERENGINE_OS_WINDOWS
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace serverengine::net::iocp {

// The port outlives every submitted Operation. stop() drains those operations
// before waking workers to exit, so the OS never completes into freed memory.
class CompletionPort final {
public:
    ~CompletionPort();

    [[nodiscard]] bool open(std::string* error_message);
    void close() noexcept;
    [[nodiscard]] bool associate(SOCKET socket, std::string* error_message);
    [[nodiscard]] bool wait(DWORD& transferred, OVERLAPPED*& overlapped);
    void wake_worker() noexcept;

    void operation_started();
    void operation_finished() noexcept;
    void wait_until_drained();

private:
    HANDLE handle_{nullptr};
    std::mutex operations_mutex_;
    std::condition_variable operations_drained_;
    std::size_t pending_operations_{0};
};

} // namespace serverengine::net::iocp
#endif
