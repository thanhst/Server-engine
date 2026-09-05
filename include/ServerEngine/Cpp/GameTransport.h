#pragma once

// Header-only convenience layer in the application. Only C ABI handles and
// caller-owned byte buffers cross the DLL boundary; no vendor headers needed.
#include <ServerEngine/C/GameTransport.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace serverengine::sdk {

enum class GameDelivery : std::uint32_t {
    Unreliable = SE_GAME_UNRELIABLE,
    ReliableOrdered = SE_GAME_RELIABLE_ORDERED
};

class GameTransportError final : public std::runtime_error {
public:
    GameTransportError(se_status status, const char* message)
        : std::runtime_error(message && *message ? message : "Game transport operation failed"), status_(status) {}
    se_status status() const noexcept { return status_; }
private:
    se_status status_;
};

struct GameEvent {
    se_game_event metadata{};
    std::vector<char> payload = std::vector<char>(256);
    std::string_view bytes() const
    {
        return metadata.payload_size ? std::string_view(payload.data(), metadata.payload_size) : std::string_view{};
    }
};

// One thread should own/poll a wrapper. Move transfers ownership; moved-from
// objects are empty. close/destruction cancels transport work, not a delivery ACK.
class GameEndpoint final {
public:
    static se_game_options default_options()
    {
        check_version();
        se_game_options options;
        se_game_options_init(&options);
        return options;
    }

    GameEndpoint() : GameEndpoint(default_options()) {}
    explicit GameEndpoint(const se_game_options& options)
    {
        check_version();
        se_error error{};
        const auto status = se_game_create(&options, &handle_, &error);
        if (status != SE_OK) { close(); throw GameTransportError(status, error.message); }
    }
    ~GameEndpoint() noexcept { close(); }
    GameEndpoint(const GameEndpoint&) = delete;
    GameEndpoint& operator=(const GameEndpoint&) = delete;
    GameEndpoint(GameEndpoint&& other) noexcept : handle_(std::exchange(other.handle_, 0)) {}
    GameEndpoint& operator=(GameEndpoint&& other) noexcept
    {
        if (this != &other) { close(); handle_ = std::exchange(other.handle_, 0); }
        return *this;
    }

    se_game_handle handle() const noexcept { return handle_; }
    void close() noexcept
    {
        const auto handle = std::exchange(handle_, 0);
        if (handle) se_game_destroy(handle, nullptr);
    }
    void listen(std::uint32_t port, const char* address = "127.0.0.1")
    {
        ensure_open();
        se_error error{};
        require(se_game_listen(handle_, address, port, &error), error);
    }
    std::uint64_t connect(std::uint32_t port, const char* address = "127.0.0.1")
    {
        ensure_open();
        se_error error{};
        std::uint64_t peer{};
        require(se_game_connect(handle_, address, port, &peer, &error), error);
        return peer; // Wait for SE_GAME_CONNECTED before sending.
    }
    se_status send(std::uint64_t peer, GameDelivery delivery, std::string_view bytes)
    {
        ensure_open();
        if (bytes.size() > UINT32_MAX) throw std::length_error("Game message exceeds ABI byte size");
        se_error error{};
        const auto status = se_game_send(handle_, peer, static_cast<std::uint32_t>(delivery),
            bytes.data(), static_cast<std::uint32_t>(bytes.size()), &error);
        if (status != SE_BACKPRESSURE) require(status, error);
        return status; // The caller decides whether/when to retry backpressure.
    }
    se_status poll(GameEvent& event, std::uint32_t timeout_ms = 0)
    {
        ensure_open();
        se_game_event_init(&event.metadata);
        se_error error{};
        auto status = se_game_poll(handle_, &event.metadata, event.payload.data(),
            static_cast<std::uint32_t>(event.payload.size()), timeout_ms, &error);
        if (status == SE_BUFFER_TOO_SMALL) {
            event.payload.resize(event.metadata.payload_size);
            status = se_game_poll(handle_, &event.metadata, event.payload.data(),
                static_cast<std::uint32_t>(event.payload.size()), 0, &error);
        }
        if (status != SE_TIMEOUT && status != SE_STOPPED) require(status, error);
        return status;
    }
    void disconnect(std::uint64_t peer)
    {
        ensure_open();
        se_error error{};
        require(se_game_disconnect(handle_, peer, &error), error);
    }

private:
    static void require(se_status status, const se_error& error)
    {
        if (status != SE_OK) throw GameTransportError(status, error.message);
    }
    static void check_version()
    {
        if (se_game_get_abi_version() != SE_GAME_ABI_VERSION)
            throw GameTransportError(SE_INVALID_STATE, "Game transport ABI mismatch");
    }
    void ensure_open() const
    {
        if (!handle_) throw std::logic_error("Game endpoint is closed or moved from");
    }
    se_game_handle handle_{};
};

} // namespace serverengine::sdk
