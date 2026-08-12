#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace serverengine::port {

struct ProcessInfo {
    std::uint32_t process_id{};
    std::filesystem::path executable_path;
    std::filesystem::path working_directory;
};

[[nodiscard]] std::uint32_t current_process_id();

[[nodiscard]] std::filesystem::path current_executable_path();

[[nodiscard]] std::filesystem::path current_working_directory();

[[nodiscard]] ProcessInfo current_process_info();

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);

} // namespace serverengine::port
