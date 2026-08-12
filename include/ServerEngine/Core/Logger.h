#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <optional>
#include <utility>

namespace serverengine::core {

enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Off
};

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;
[[nodiscard]] std::optional<LogLevel> parse_log_level(std::string_view value);

struct LoggerOptions {
    LogLevel minimum_level{LogLevel::Info};
    bool console_enabled{true};
    std::filesystem::path file_path{};
    bool append_file{true};
};

class Logger final {
public:
    explicit Logger(LoggerOptions options = {});
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    [[nodiscard]] bool initialize();
    void close();

    [[nodiscard]] bool should_log(LogLevel level) const noexcept;

    void log(LogLevel level, std::string_view message);

    template <typename... Args>
    void trace(Args&&... args)
    {
        write(LogLevel::Trace, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(Args&&... args)
    {
        write(LogLevel::Debug, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(Args&&... args)
    {
        write(LogLevel::Info, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warning(Args&&... args)
    {
        write(LogLevel::Warning, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(Args&&... args)
    {
        write(LogLevel::Error, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void critical(Args&&... args)
    {
        write(LogLevel::Critical, std::forward<Args>(args)...);
    }

private:
    [[nodiscard]] bool initialize_locked();
    [[nodiscard]] static std::string format_line(LogLevel level, std::string_view message);

    template <typename... Args>
    void write(LogLevel level, Args&&... args)
    {
        if (!should_log(level)) {
            return;
        }

        std::ostringstream message;
        (message << ... << std::forward<Args>(args));
        log(level, message.str());
    }

    LoggerOptions options_;
    std::mutex mutex_;
    std::ofstream file_;
    bool initialized_{false};
};

} // namespace serverengine::core
