#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace serverengine::core {

class Buffer final {
public:
    using Byte = std::uint8_t;

    Buffer() = default;
    explicit Buffer(std::vector<Byte> bytes);
    Buffer(const Byte* data, std::size_t size);

    [[nodiscard]] static Buffer from_text(std::string_view text);

    [[nodiscard]] const Byte* data() const noexcept;
    [[nodiscard]] Byte* data() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const std::vector<Byte>& bytes() const noexcept;

    void clear();
    void append(const Byte* data, std::size_t size);
    void append(std::string_view text);

    [[nodiscard]] std::string to_text() const;

private:
    std::vector<Byte> bytes_;
};

} // namespace serverengine::core
