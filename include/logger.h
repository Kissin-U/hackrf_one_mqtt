#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <string>
#include <chrono>
#include <iomanip> // For std::put_time
#include <sstream> // For string stream
#include <atomic>  // For atomic log level
#include <algorithm> // For std::transform (string to upper)

namespace hackrf_mqtt {
namespace logger {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    NONE // To disable all logging
};

// Global atomic variable for current log level
// Default to INFO if not initialized
static std::atomic<LogLevel> current_log_level(LogLevel::INFO);

// Helper to convert string to LogLevel
inline LogLevel string_to_log_level(const std::string& level_str) {
    std::string upper_level_str = level_str;
    std::transform(upper_level_str.begin(), upper_level_str.end(), upper_level_str.begin(), ::toupper);

    if (upper_level_str == "DEBUG") return LogLevel::DEBUG;
    if (upper_level_str == "INFO") return LogLevel::INFO;
    if (upper_level_str == "WARNING") return LogLevel::WARNING;
    if (upper_level_str == "ERROR") return LogLevel::ERROR;
    if (upper_level_str == "NONE") return LogLevel::NONE;
    return LogLevel::INFO; // Default if unrecognized
}

// Initialize the logger with a specific level
inline void init(LogLevel level) {
    current_log_level.store(level);
}

inline void init(const std::string& level_str) {
    init(string_to_log_level(level_str));
}

// Helper to get current timestamp as string
inline std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;

#ifdef _WIN32
    localtime_s(&now_tm, &now_c); // Windows specific
#else
    localtime_r(&now_c, &now_tm); // POSIX specific
#endif

    std::stringstream ss;
    ss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    
    // Add milliseconds
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

// Generic log function
template<typename... Args>
inline void log(LogLevel level, const char* level_str, Args&&... args) {
    if (level >= current_log_level.load()) {
        std::stringstream message_ss;
        // C++17 fold expression for cleaner parameter packing into stringstream
        (message_ss << ... << std::forward<Args>(args)); 
        
        std::ostream& out_stream = (level == LogLevel::ERROR || level == LogLevel::WARNING) ? std::cerr : std::cout;
        out_stream << "[" << get_timestamp() << "] [" << level_str << "] " << message_ss.str() << std::endl;
    }
}

// Logging macros for convenience
#define LOG_DEBUG(...) hackrf_mqtt::logger::log(hackrf_mqtt::logger::LogLevel::DEBUG, "DEBUG", __VA_ARGS__)
#define LOG_INFO(...)  hackrf_mqtt::logger::log(hackrf_mqtt::logger::LogLevel::INFO,  "INFO",  __VA_ARGS__)
#define LOG_WARN(...)  hackrf_mqtt::logger::log(hackrf_mqtt::logger::LogLevel::WARNING,"WARN",  __VA_ARGS__)
#define LOG_ERROR(...) hackrf_mqtt::logger::log(hackrf_mqtt::logger::LogLevel::ERROR, "ERROR", __VA_ARGS__)

} // namespace logger
} // namespace hackrf_mqtt

#endif // LOGGER_H
