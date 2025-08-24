#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>
#include <chrono>

#include "Stream.h"

namespace multiscreen {

    class StreamManager {
    public:
        ~StreamManager();

        // управление составом
        bool  addStream(const std::string& name, const std::string& url);
        // >>> ДОБАВЛЕНО: перегрузка с decoder (для совместимости с WebServer)
        // Минимальная: 3-й аргумент игнорируется и вызывает 2-аргументную версию.
        bool  addStream(const std::string& name, const std::string& url, const std::string& decoder);
        bool  removeStream(const std::string& name);

        // запуск/остановка всех
        void  startAll();
        void  stopAll();

        // управление одним стримом
        bool  startStream(const std::string& name);
        bool  stopStream(const std::string& name);
        bool  restartStream(const std::string& name);

        // отчёты
        std::vector<StreamStats> getAllStats();

        // конфиги
        bool  loadConfig(const std::string& jsonPath);
        void  loadFromList(const std::vector<std::pair<std::string, std::string>>& items);
        size_t size() const;

    private:
        void  monitor_loop();
        void  restart_stream_unlocked(const std::string&);

        struct WDState {
            int low_decode_consec = 0;
            std::deque<int> kbps_hist;
            std::uint64_t last_cc = 0;
            std::chrono::steady_clock::time_point last_cc_t{};
            std::string last_status;
        };

        std::unordered_map<std::string, std::shared_ptr<Stream>> m_streams;
        mutable std::mutex m_mutex;

        std::thread m_mon;
        std::atomic<bool> m_mon_run{ false };

        std::unordered_map<std::string, WDState> m_wd;
    };

} // namespace multiscreen
