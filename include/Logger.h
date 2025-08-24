#pragma once
#include <string>
#include <fstream>
#include <deque>
#include <vector>
#include <mutex>
#include <chrono>
#include <filesystem>

namespace multiscreen {

    enum class LogLevel { Debug, Info, Warning, Error };

    struct LogEntry {
        std::chrono::system_clock::time_point ts;
        LogLevel level;
        std::string message;
    };

    class Logger {
    public:
        // Инициализация/завершение без реентерации в log()
        static void initialize(const std::string& filename = "logs/app.log");
        static void shutdown();

        static void log(LogLevel level, const std::string& message);
        static inline void debug(const std::string& m) { log(LogLevel::Debug, m); }
        static inline void info(const std::string& m) { log(LogLevel::Info, m); }
        static inline void warning(const std::string& m) { log(LogLevel::Warning, m); }
        static inline void error(const std::string& m) { log(LogLevel::Error, m); }

        static std::vector<LogEntry> getRecentLogs(size_t count = 50);

    private:
        // ВАЖНО: recursive_mutex + try_lock в реализации для защиты от самодедлоков
        static std::recursive_mutex s_mutex;
        static std::ofstream        s_file;
        static std::deque<LogEntry> s_recent;
        static constexpr size_t     s_max_recent = 1000;

        static const char* levelToStr(LogLevel l);
    };

} // namespace multiscreen
