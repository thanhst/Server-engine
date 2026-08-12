#include <ServerEngine/Core/Config.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <sstream>

namespace serverengine::core {

namespace {

[[nodiscard]] std::string_view trim_view(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }

    return value;
}

[[nodiscard]] std::string trim_copy(std::string_view value)
{
    const auto trimmed = trim_view(value);
    return std::string(trimmed.begin(), trimmed.end());
}

[[nodiscard]] std::string lowercase_copy(std::string_view value)
{
    std::string result(value.begin(), value.end());
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

void set_error(std::string* error_message, const std::filesystem::path& path, int line_number, std::string_view detail)
{
    if (error_message == nullptr) {
        return;
    }

    std::ostringstream message;
    message << path.string() << ':' << line_number << ": " << detail;
    *error_message = message.str();
}

} // namespace

bool Config::load_file(const std::filesystem::path& path, std::string* error_message)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        if (error_message != nullptr) {
            *error_message = "Failed to open config file: " + path.string();
        }
        return false;
    }

    values_.clear();

    std::string section;
    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        ++line_number;

        const auto trimmed = trim_view(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
            continue;
        }

        if (trimmed.front() == '[') {
            if (trimmed.back() != ']') {
                set_error(error_message, path, line_number, "section header is missing ']'");
                return false;
            }

            section = trim_copy(trimmed.substr(1, trimmed.size() - 2));
            if (section.empty()) {
                set_error(error_message, path, line_number, "section name is empty");
                return false;
            }
            continue;
        }

        const auto separator = trimmed.find('=');
        if (separator == std::string_view::npos) {
            set_error(error_message, path, line_number, "expected key=value");
            return false;
        }

        auto key = trim_copy(trimmed.substr(0, separator));
        auto value = trim_copy(trimmed.substr(separator + 1));

        if (key.empty()) {
            set_error(error_message, path, line_number, "key is empty");
            return false;
        }

        if (!section.empty()) {
            key = section + "." + key;
        }

        values_[std::move(key)] = std::move(value);
    }

    return true;
}

void Config::clear()
{
    values_.clear();
}

void Config::set(std::string key, std::string value)
{
    values_[std::move(key)] = std::move(value);
}

bool Config::contains(std::string_view key) const
{
    return values_.find(std::string(key)) != values_.end();
}

std::optional<std::string> Config::get_string(std::string_view key) const
{
    const auto iterator = values_.find(std::string(key));
    if (iterator == values_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::string Config::get_string_or(std::string_view key, std::string fallback) const
{
    if (const auto value = get_string(key)) {
        return *value;
    }

    return fallback;
}

std::optional<int> Config::get_int(std::string_view key) const
{
    const auto value = get_string(key);
    if (!value) {
        return std::nullopt;
    }

    const auto trimmed = trim_view(*value);
    int result = 0;
    const auto* begin = trimmed.data();
    const auto* end = begin + trimmed.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }

    return result;
}

int Config::get_int_or(std::string_view key, int fallback) const
{
    if (const auto value = get_int(key)) {
        return *value;
    }

    return fallback;
}

std::optional<bool> Config::get_bool(std::string_view key) const
{
    const auto value = get_string(key);
    if (!value) {
        return std::nullopt;
    }

    const auto lowered = lowercase_copy(trim_view(*value));
    if (lowered == "true" || lowered == "yes" || lowered == "on" || lowered == "1") {
        return true;
    }

    if (lowered == "false" || lowered == "no" || lowered == "off" || lowered == "0") {
        return false;
    }

    return std::nullopt;
}

bool Config::get_bool_or(std::string_view key, bool fallback) const
{
    if (const auto value = get_bool(key)) {
        return *value;
    }

    return fallback;
}

std::size_t Config::size() const noexcept
{
    return values_.size();
}

} // namespace serverengine::core
