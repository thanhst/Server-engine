#include "CompletionPort.h"

#if SERVERENGINE_OS_WINDOWS
namespace serverengine::net::iocp {

CompletionPort::~CompletionPort()
{
    close();
}

bool CompletionPort::open(std::string* error_message)
{
    handle_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (handle_ == nullptr) {
        set_error(error_message, "CreateIoCompletionPort() failed: " + windows_error_message(::GetLastError()));
        return false;
    }
    return true;
}

void CompletionPort::close() noexcept
{
    if (handle_ != nullptr) {
        ::CloseHandle(handle_);
        handle_ = nullptr;
    }
}

bool CompletionPort::associate(SOCKET socket, std::string* error_message)
{
    if (::CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), handle_, 0, 0) == nullptr) {
        set_error(error_message, "CreateIoCompletionPort(client) failed: " + windows_error_message(::GetLastError()));
        return false;
    }
    return true;
}

bool CompletionPort::wait(DWORD& transferred, OVERLAPPED*& overlapped)
{
    ULONG_PTR unused_key = 0;
    return ::GetQueuedCompletionStatus(handle_, &transferred, &unused_key, &overlapped, INFINITE) != FALSE;
}

void CompletionPort::wake_worker() noexcept
{
    ::PostQueuedCompletionStatus(handle_, 0, 0, nullptr);
}

void CompletionPort::operation_started()
{
    std::lock_guard<std::mutex> lock(operations_mutex_);
    ++pending_operations_;
}

void CompletionPort::operation_finished() noexcept
{
    std::lock_guard<std::mutex> lock(operations_mutex_);
    if (--pending_operations_ == 0) {
        operations_drained_.notify_all();
    }
}

void CompletionPort::wait_until_drained()
{
    std::unique_lock<std::mutex> lock(operations_mutex_);
    operations_drained_.wait(lock, [this] { return pending_operations_ == 0; });
}

} // namespace serverengine::net::iocp
#endif
