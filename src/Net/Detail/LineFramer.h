#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace serverengine::net::detail {

// TCP delivers chunks of bytes, not messages. Each connection owns one framer.
// Call append for every read, then try_pop until it returns false.
class LineFramer final {
public:
    explicit LineFramer(std::size_t max_message_bytes)
        : max_message_bytes_(max_message_bytes)
    {
    }

    // The limit counts bytes before LF (including an optional CR). Checking
    // each line separately accepts valid traffic regardless of packet grouping.
    // A failed append is terminal: the caller must disconnect the client.
    // If a read contains an invalid line, none of that read is dispatched.
    [[nodiscard]] bool append(std::string_view bytes)
    {
        if (failed_) {
            return false;
        }
        if (bytes.empty()) {
            return true;
        }

        for (const char byte : bytes) {
            if (byte == '\n') {
                current_line_bytes_ = 0;
            } else {
                if (current_line_bytes_ == max_message_bytes_) {
                    failed_ = true;
                    return false;
                }
                ++current_line_bytes_;
            }
        }

        // Compact once per read, rather than moving the buffer for every line.
        pending_.erase(0, consumed_bytes_);
        consumed_bytes_ = 0;
        pending_.append(bytes.data(), bytes.size());
        return true;
    }

    [[nodiscard]] bool try_pop(std::string& line)
    {
        if (failed_) {
            return false;
        }
        const auto newline = pending_.find('\n', consumed_bytes_);
        if (newline == std::string::npos) {
            return false;
        }

        line.assign(pending_, consumed_bytes_, newline - consumed_bytes_);
        consumed_bytes_ = newline + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return true;
    }

private:
    const std::size_t max_message_bytes_;
    std::string pending_;
    std::size_t consumed_bytes_{0};
    std::size_t current_line_bytes_{0};
    bool failed_{false};
};

} // namespace serverengine::net::detail
