#include "Logger.h"
#include <iomanip>
#include <ctime>
#include <algorithm>

namespace multiscreen {

    std::recursive_mutex Logger::s_mutex;
    std::ofstream        Logger::s_file;
    std::deque<LogEntry> Logger::s_recent;

    static std::string tsToString(std::chrono::system_clock::time_point tp) {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return buf;
    }

    void Logger::initialize(const std::string& filename) {
        try {
            std::filesystem::path p(filename);
            if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
            // не блокируемс€: если замок зан€т Ч пропускаем
            std::unique_lock<std::recursive_mutex> lk(s_mutex, std::try_to_lock);
            if (lk.owns_lock()) {
                s_file.open(filename, std::ios::app);
                if (s_file.is_open()) {
                    auto now = std::chrono::system_clock::now();
                    s_file << tsToString(now) << " [INFO] Logger initialized" << std::endl;
                }
            }
        }
        catch (...) {
            // глушим исключени€ логгера
        }
    }

    void Logger::shutdown() {
        std::unique_lock<std::recursive_mutex> lk(s_mutex, std::try_to_lock);
        if (lk.owns_lock() && s_file.is_open()) {
            auto now = std::chrono::system_clock::now();
            s_file << tsToString(now) << " [INFO] Logger shutdown" << std::endl;
            s_file.close();
        }
    }

    const char* Logger::levelToStr(LogLevel l) {
        switch (l) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
        }
    }

    void Logger::log(LogLevel level, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        std::unique_lock<std::recursive_mutex> lk(s_mutex, std::try_to_lock);
        if (!lk.owns_lock()) return; // замок зан€т Ч тихо пропускаем
        s_recent.push_back({ now, level, message });
        if (s_recent.size() > s_max_recent) s_recent.pop_front();
        if (s_file.is_open()) {
            s_file << tsToString(now) << " [" << levelToStr(level) << "] " << message << std::endl;
        }
    }

    std::vector<LogEntry> Logger::getRecentLogs(size_t count) {
        std::unique_lock<std::recursive_mutex> lk(s_mutex, std::try_to_lock);
        if (!lk.owns_lock()) return {};
        count = std::min(count, s_recent.size());
        std::vector<LogEntry> v;
        v.reserve(count);
        auto it = s_recent.end();
        for (size_t i = 0; i < count; ++i) {
            --it;
            v.push_back(*it);
        }
        return v;
    }

} // namespace multiscreen
