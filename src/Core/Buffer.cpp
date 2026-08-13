#include <ServerEngine/Core/Buffer.h>

#include <utility>

namespace serverengine::core {

Buffer::Buffer(std::vector<Byte> bytes)
    : bytes_(std::move(bytes))
{
}

Buffer::Buffer(const Byte* data, std::size_t size)
{
    append(data, size);
}

Buffer Buffer::from_text(std::string_view text)
{
    Buffer buffer;
    buffer.append(text);
    return buffer;
}

const Buffer::Byte* Buffer::data() const noexcept
{
    return bytes_.data();
}

Buffer::Byte* Buffer::data() noexcept
{
    return bytes_.data();
}

std::size_t Buffer::size() const noexcept
{
    return bytes_.size();
}

bool Buffer::empty() const noexcept
{
    return bytes_.empty();
}

const std::vector<Buffer::Byte>& Buffer::bytes() const noexcept
{
    return bytes_;
}

void Buffer::clear()
{
    bytes_.clear();
}

void Buffer::append(const Byte* data, std::size_t size)
{
    if (data == nullptr || size == 0) {
        return;
    }

    bytes_.insert(bytes_.end(), data, data + size);
}

void Buffer::append(std::string_view text)
{
    const auto* begin = reinterpret_cast<const Byte*>(text.data());
    append(begin, text.size());
}

std::string Buffer::to_text() const
{
    return std::string(bytes_.begin(), bytes_.end());
}

} // namespace serverengine::core
