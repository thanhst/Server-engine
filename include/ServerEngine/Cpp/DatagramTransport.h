#pragma once

// Header-only convenience layer in the application. Only C ABI handles and
// caller-owned byte buffers cross the DLL boundary; no vendor headers needed.
#include <ServerEngine/C/DatagramTransport.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace serverengine::sdk {

enum class DatagramDelivery : std::uint32_t {
    Unreliable = SE_DATAGRAM_UNRELIABLE,
    ReliableOrdered = SE_DATAGRAM_RELIABLE_ORDERED
};

class DatagramTransportError final : public std::runtime_error {
public:
    DatagramTransportError(se_status status, const char* message)
        : std::runtime_error(message && *message ? message : "Datagram transport operation failed"), status_(status) {}
    se_status status() const noexcept { return status_; }
private:
    se_status status_;
};

struct DatagramEvent {
    se_datagram_event metadata{};
    std::vector<char> payload = std::vector<char>(256);
    std::string_view bytes() const
    {
        return metadata.payload_size ? std::string_view(payload.data(), metadata.payload_size) : std::string_view{};
    }
};

// One thread should own/poll a wrapper. Move transfers ownership; moved-from
// objects are empty. close/destruction cancels transport work, not a delivery ACK.
class DatagramEndpoint final {
public:
    static se_datagram_options default_options()
    {
        check_version();
        se_datagram_options options;
        se_datagram_options_init(&options);
        return options;
    }

    DatagramEndpoint() : DatagramEndpoint(default_options()) {}
    explicit DatagramEndpoint(const se_datagram_options& options)
    {
        check_version();
        se_error error{};
        const auto status = se_datagram_create(&options, &handle_, &error);
        if (status != SE_OK) { close(); throw DatagramTransportError(status, error.message); }
    }
    ~DatagramEndpoint() noexcept { close(); }
    DatagramEndpoint(const DatagramEndpoint&) = delete;
    DatagramEndpoint& operator=(const DatagramEndpoint&) = delete;
    DatagramEndpoint(DatagramEndpoint&& other) noexcept : handle_(std::exchange(other.handle_, 0)) {}
    DatagramEndpoint& operator=(DatagramEndpoint&& other) noexcept
    {
        if (this != &other) { close(); handle_ = std::exchange(other.handle_, 0); }
        return *this;
    }

    se_datagram_handle handle() const noexcept { return handle_; }
    void close() noexcept
    {
        const auto handle = std::exchange(handle_, 0);
        if (handle) se_datagram_destroy(handle, nullptr);
    }
    void listen(std::uint32_t port, const char* address = "127.0.0.1")
    {
        ensure_open();
        se_error error{};
        require(se_datagram_listen(handle_, address, port, &error), error);
    }
    std::uint64_t connect(std::uint32_t port, const char* address = "127.0.0.1")
    {
        ensure_open();
        se_error error{};
        std::uint64_t peer{};
        require(se_datagram_connect(handle_, address, port, &peer, &error), error);
        return peer; // Wait for SE_DATAGRAM_CONNECTED before sending.
    }
    se_status send(std::uint64_t peer, DatagramDelivery delivery, std::string_view bytes)
    {
        ensure_open();
        if (bytes.size() > UINT32_MAX) throw std::length_error("Datagram message exceeds ABI byte size");
        se_error error{};
        const auto status = se_datagram_send(handle_, peer, static_cast<std::uint32_t>(delivery),
            bytes.data(), static_cast<std::uint32_t>(bytes.size()), &error);
        if (status != SE_BACKPRESSURE) require(status, error);
        return status; // The caller decides whether/when to retry backpressure.
    }
    se_status poll(DatagramEvent& event, std::uint32_t timeout_ms = 0)
    {
        ensure_open();
        se_datagram_event_init(&event.metadata);
        se_error error{};
        auto status = se_datagram_poll(handle_, &event.metadata, event.payload.data(),
            static_cast<std::uint32_t>(event.payload.size()), timeout_ms, &error);
        if (status == SE_BUFFER_TOO_SMALL) {
            event.payload.resize(event.metadata.payload_size);
            status = se_datagram_poll(handle_, &event.metadata, event.payload.data(),
                static_cast<std::uint32_t>(event.payload.size()), 0, &error);
        }
        if (status != SE_TIMEOUT && status != SE_STOPPED) require(status, error);
        return status;
    }
    void disconnect(std::uint64_t peer)
    {
        ensure_open();
        se_error error{};
        require(se_datagram_disconnect(handle_, peer, &error), error);
    }

private:
    static void require(se_status status, const se_error& error)
    {
        if (status != SE_OK) throw DatagramTransportError(status, error.message);
    }
    static void check_version()
    {
        if (se_datagram_get_abi_version() != SE_DATAGRAM_ABI_VERSION)
            throw DatagramTransportError(SE_INVALID_STATE, "Datagram transport ABI mismatch");
    }
    void ensure_open() const
    {
        if (!handle_) throw std::logic_error("Datagram endpoint is closed or moved from");
    }
    se_datagram_handle handle_{};
};

} // namespace serverengine::sdk
