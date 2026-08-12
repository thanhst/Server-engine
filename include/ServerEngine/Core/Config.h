#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace serverengine::core {

class Config final {
public:
    [[nodiscard]] bool load_file(const std::filesystem::path& path, std::string* error_message = nullptr);

    void clear();
    void set(std::string key, std::string value);

    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::optional<std::string> get_string(std::string_view key) const;
    [[nodiscard]] std::string get_string_or(std::string_view key, std::string fallback) const;
    [[nodiscard]] std::optional<int> get_int(std::string_view key) const;
    [[nodiscard]] int get_int_or(std::string_view key, int fallback) const;
    [[nodiscard]] std::optional<bool> get_bool(std::string_view key) const;
    [[nodiscard]] bool get_bool_or(std::string_view key, bool fallback) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<std::string, std::string> values_;
};

} // namespace serverengine::core
