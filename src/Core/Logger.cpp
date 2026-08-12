#include <ServerEngine/Core/Logger.h>

#include <ServerEngine/Port/Clock.h>

#include <iomanip>
#include <iostream>
#include <system_error>
#include <algorithm>
#include <cctype>

namespace serverengine::core {

namespace {

[[nodiscard]] std::ostream& console_stream(LogLevel level)
{
    if (level >= LogLevel::Warning) {
        return std::cerr;
    }

    return std::cout;
}

[[nodiscard]] std::string lowercase_copy(std::string_view value)
{
    std::string result(value.begin(), value.end());
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

} // namespace

std::string_view to_string(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Critical:
        return "CRITICAL";
    case LogLevel::Off:
        return "OFF";
    }

    return "UNKNOWN";
}

std::optional<LogLevel> parse_log_level(std::string_view value)
{
    const auto lowered = lowercase_copy(value);
    if (lowered == "trace") {
        return LogLevel::Trace;
    }
    if (lowered == "debug") {
        return LogLevel::Debug;
    }
    if (lowered == "info") {
        return LogLevel::Info;
    }
    if (lowered == "warning" || lowered == "warn") {
        return LogLevel::Warning;
    }
    if (lowered == "error") {
        return LogLevel::Error;
    }
    if (lowered == "critical" || lowered == "fatal") {
        return LogLevel::Critical;
    }
    if (lowered == "off" || lowered == "none") {
        return LogLevel::Off;
    }

    return std::nullopt;
}

Logger::Logger(LoggerOptions options)
    : options_(std::move(options))
{
}

Logger::~Logger()
{
    close();
}

bool Logger::initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialize_locked();
}

void Logger::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }

    initialized_ = false;
}

bool Logger::should_log(LogLevel level) const noexcept
{
    if (level == LogLevel::Off || options_.minimum_level == LogLevel::Off) {
        return false;
    }

    return static_cast<int>(level) >= static_cast<int>(options_.minimum_level);
}

void Logger::log(LogLevel level, std::string_view message)
{
    if (!should_log(level)) {
        return;
    }

    const auto line = format_line(level, message);

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialize_locked()) {
        return;
    }

    if (options_.console_enabled) {
        console_stream(level) << line << '\n';
    }

    if (file_.is_open()) {
        file_ << line << '\n';
        if (level >= LogLevel::Warning) {
            file_.flush();
        }
    }
}

bool Logger::initialize_locked()
{
    if (initialized_) {
        return true;
    }

    if (!options_.file_path.empty()) {
        const auto parent_path = options_.file_path.parent_path();
        if (!parent_path.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent_path, error);
            if (error) {
                return false;
            }
        }

        auto mode = std::ios::out;
        mode |= options_.append_file ? std::ios::app : std::ios::trunc;
        file_.open(options_.file_path, mode);
        if (!file_.is_open()) {
            return false;
        }
    }

    initialized_ = true;
    return true;
}

std::string Logger::format_line(LogLevel level, std::string_view message)
{
    const auto now = port::local_date_time();

    std::ostringstream line;
    line << std::setfill('0')
         << std::setw(4) << now.year << '-'
         << std::setw(2) << now.month << '-'
         << std::setw(2) << now.day << ' '
         << std::setw(2) << now.hour << ':'
         << std::setw(2) << now.minute << ':'
         << std::setw(2) << now.second << '.'
         << std::setw(3) << now.millisecond
         << " [" << to_string(level) << "] "
         << message;

    return line.str();
}

} // namespace serverengine::core
