#include "StreamManager.h"
#include "Logger.h"
#include "Settings.h"
#include "Alerts.h"
#include "Stream.h"

#include <nlohmann/json.hpp>
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <climits>

using json = nlohmann::json;
using namespace std::chrono_literals;

namespace multiscreen {

    // ================= thresholds & webhook (settings.json) =================
    namespace {

        struct Thresholds {
            double      fps_warn_ratio = 0.70;
            double      fps_crit_ratio = 0.40;
            int         bitrate_warn_kbps = 300;
            int         bitrate_crit_kbps = 100;
            int         stall_warn_ms = 3000;
            int         stall_crit_ms = 7000;

            bool        webhook_enabled = false;
            std::string webhook_url;
            int         webhook_timeout_ms = 1500;
        };

        static Thresholds load_thresholds_once() {
            static std::once_flag once;
            static Thresholds th;
            std::call_once(once, [] {
                std::ifstream f("config/settings.json");
                if (!f.good()) return;
                try {
                    json j = json::object();
                    f >> j;

                    const auto tj = (j.contains("thresholds") && j["thresholds"].is_object())
                        ? j["thresholds"] : json::object();
                    const auto wj = (j.contains("alerts") && j["alerts"].is_object()
                        && j["alerts"].contains("webhook") && j["alerts"]["webhook"].is_object())
                        ? j["alerts"]["webhook"] : json::object();

                    if (tj.contains("fps") && tj["fps"].is_object()) {
                        const auto& fps = tj["fps"];
                        if (fps.contains("warn_ratio") && fps["warn_ratio"].is_number())
                            th.fps_warn_ratio = std::clamp(fps["warn_ratio"].get<double>(), 0.0, 1.0);
                        if (fps.contains("crit_ratio") && fps["crit_ratio"].is_number())
                            th.fps_crit_ratio = std::clamp(fps["crit_ratio"].get<double>(), 0.0, 1.0);
                    }
                    if (tj.contains("bitrate") && tj["bitrate"].is_object()) {
                        const auto& br = tj["bitrate"];
                        if (br.contains("warn_kbps") && br["warn_kbps"].is_number_integer())
                            th.bitrate_warn_kbps = std::max(0, br["warn_kbps"].get<int>());
                        if (br.contains("crit_kbps") && br["crit_kbps"].is_number_integer())
                            th.bitrate_crit_kbps = std::max(0, br["crit_kbps"].get<int>());
                    }
                    if (tj.contains("stall") && tj["stall"].is_object()) {
                        const auto& st = tj["stall"];
                        if (st.contains("warn_ms") && st["warn_ms"].is_number_integer())
                            th.stall_warn_ms = std::max(0, st["warn_ms"].get<int>());
                        if (st.contains("crit_ms") && st["crit_ms"].is_number_integer())
                            th.stall_crit_ms = std::max(0, st["crit_ms"].get<int>());
                    }

                    if (wj.contains("enabled") && wj["enabled"].is_boolean())
                        th.webhook_enabled = wj["enabled"].get<bool>();
                    if (wj.contains("url") && wj["url"].is_string())
                        th.webhook_url = wj["url"].get<std::string>();
                    if (wj.contains("timeout_ms") && wj["timeout_ms"].is_number_integer())
                        th.webhook_timeout_ms = std::max(200, wj["timeout_ms"].get<int>());
                }
                catch (...) {
                    // keep defaults
                }
                });
            return th;
        }

        static void send_webhook(
            const Thresholds& th,
            const std::string& channel_name,
            const std::string& service_name,
            const std::string& new_status,
            double input_fps, double decode_fps, int bitrate_kbps, int stall_ms /*=0*/)
        {
            if (!th.webhook_enabled || th.webhook_url.empty()) return;

            static std::mutex http_mx;
            std::lock_guard<std::mutex> lk(http_mx);

            httplib::Client cli(th.webhook_url.c_str());
            cli.set_connection_timeout(0, th.webhook_timeout_ms * 1000);
            cli.set_read_timeout(0, th.webhook_timeout_ms * 1000);
            cli.set_write_timeout(0, th.webhook_timeout_ms * 1000);

            json payload = {
                {"event","stream_status"},
                {"channel", channel_name},
                {"service", service_name},
                {"status", new_status},
                {"metrics", {
                    {"input_fps", input_fps},
                    {"decode_fps", decode_fps},
                    {"fps_ratio", (input_fps > 0.0 ? decode_fps / input_fps : 1.0)},
                    {"bitrate_kbps", bitrate_kbps},
                    {"stall_ms", stall_ms}
                }},
                {"ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch()).count()}
            };
            (void)cli.Post("/", payload.dump(), "application/json");
        }

    } // anonymous

    // ================== реализация StreamManager ==================

    StreamManager::~StreamManager() {
        stopAll();
    }

    bool StreamManager::loadConfig(const std::string& jsonPath) {
        std::ifstream f(jsonPath, std::ios::binary);
        if (!f) {
            Logger::warning(std::string("Cannot open streams json: ") + jsonPath);
            return false;
        }

        json root = json::object();
        try { f >> root; }
        catch (const std::exception& ex) {
            Logger::error(std::string("streams.json parse error: ") + ex.what());
            return false;
        }

        std::vector<std::pair<std::string, std::string>> list;
        if (root.is_array()) {
            for (const auto& it : root) {
                if (!it.is_object()) continue;
                const std::string name = it.value("name", "");
                const std::string url = it.value("url", "");
                if (!name.empty() && !url.empty()) list.emplace_back(name, url);
            }
        }
        else if (root.is_object()) {
            const auto arr = root.contains("streams") ? root["streams"] : json::array();
            if (arr.is_array()) {
                for (const auto& it : arr) {
                    if (!it.is_object()) continue;
                    const std::string name = it.value("name", "");
                    const std::string url = it.value("url", "");
                    if (!name.empty() && !url.empty()) list.emplace_back(name, url);
                }
            }
        }

        loadFromList(list);
        return true;
    }

    void StreamManager::loadFromList(const std::vector<std::pair<std::string, std::string>>& lst) {
        std::lock_guard<std::mutex> lk(m_mutex);

        for (auto& kv : m_streams) {
            if (kv.second) kv.second->stop();
        }
        m_streams.clear();
        m_wd.clear();

        for (const auto& p : lst) {
            const std::string& name = p.first;
            const std::string& url = p.second;

            auto sp = std::make_shared<Stream>(name, url);
            m_streams[name] = sp;

            m_wd[name] = WDState{};
            m_wd[name].last_status = "ok";
            m_wd[name].last_cc = 0;
            m_wd[name].last_cc_t = std::chrono::steady_clock::now();
        }
    }

    size_t StreamManager::size() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_streams.size();
    }

    void StreamManager::startAll() {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            for (auto& kv : m_streams) {
                if (kv.second) kv.second->start();
            }
        }
        if (!m_mon_run.load()) {
            m_mon_run = true;
            if (m_mon.joinable()) m_mon.join();
            m_mon = std::thread([this] { monitor_loop(); });
        }
    }

    void StreamManager::stopAll() {
        m_mon_run = false;
        if (m_mon.joinable()) m_mon.join();

        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& kv : m_streams) {
            if (kv.second) kv.second->stop();
        }
    }

    bool StreamManager::addStream(const std::string& name, const std::string& url) {
        std::lock_guard<std::mutex> lk(m_mutex);

        auto it = m_streams.find(name);
        if (it != m_streams.end()) {
            if (it->second) it->second->stop();
            it->second.reset();
        }

        auto sp = std::make_shared<Stream>(name, url);
        m_streams[name] = sp;

        m_wd[name] = WDState{};
        m_wd[name].last_status = "ok";
        m_wd[name].last_cc = 0;
        m_wd[name].last_cc_t = std::chrono::steady_clock::now();

        sp->start();
        return true;
    }

    bool StreamManager::addStream(const std::string& name, const std::string& url, const std::string& /*decoder*/) {
        std::lock_guard<std::mutex> lk(m_mutex);

        auto it = m_streams.find(name);
        if (it != m_streams.end()) {
            if (it->second) it->second->stop();
            it->second.reset();
        }

        auto sp = std::make_shared<Stream>(name, url);
        m_streams[name] = sp;

        m_wd[name] = WDState{};
        m_wd[name].last_status = "ok";
        m_wd[name].last_cc = 0;
        m_wd[name].last_cc_t = std::chrono::steady_clock::now();

        sp->start();
        return true;
    }

    bool StreamManager::removeStream(const std::string& name) {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_streams.find(name);
        if (it == m_streams.end()) return false;
        if (it->second) it->second->stop();
        m_streams.erase(it);
        m_wd.erase(name);
        return true;
    }

    bool StreamManager::startStream(const std::string& name) {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_streams.find(name);
        if (it == m_streams.end() || !it->second) return false;
        it->second->start();
        return true;
    }

    bool StreamManager::stopStream(const std::string& name) {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_streams.find(name);
        if (it == m_streams.end() || !it->second) return false;
        it->second->stop();
        return true;
    }

    bool StreamManager::restartStream(const std::string& name) {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_streams.find(name);
        if (it == m_streams.end() || !it->second) return false;
        it->second->stop();
        it->second->start();
        return true;
    }

    std::vector<StreamStats> StreamManager::getAllStats() {
        std::vector<StreamStats> out;
        std::lock_guard<std::mutex> lk(m_mutex);
        out.reserve(m_streams.size());
        for (auto& kv : m_streams) {
            auto s = kv.second->stats();
            auto wd_it = m_wd.find(kv.first);
            if (wd_it != m_wd.end()) {
                s.status = wd_it->second.last_status;
            }
            out.push_back(std::move(s));
        }
        return out;
    }

    void StreamManager::monitor_loop() {
        const Thresholds TH = load_thresholds_once();

        while (m_mon_run.load()) {
            std::vector<std::pair<std::string, StreamStats>> stats;
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                stats.reserve(m_streams.size());
                for (auto& kv : m_streams) {
                    if (!kv.second) continue;
                    stats.emplace_back(kv.first, kv.second->stats());
                }
            }

            for (auto& it : stats) {
                const std::string& name = it.first;
                const StreamStats& st = it.second;

                double input_fps = st.input_fps;  if (input_fps < 0.0) input_fps = 0.0;
                double decode_fps = st.decode_fps; if (decode_fps < 0.0) decode_fps = 0.0;

                int bitrate = static_cast<int>(st.bitrate_kbps);
                if (bitrate < 0) bitrate = 0;

                int stall_ms = 0; // real stall field can be wired later

                double ratio = 1.0;
                if (input_fps > 0.0001) ratio = decode_fps / input_fps;

                std::string status = "ok";
                if (ratio <= TH.fps_crit_ratio || bitrate <= TH.bitrate_crit_kbps || stall_ms >= TH.stall_crit_ms) {
                    status = "crit";
                }
                else if (ratio <= TH.fps_warn_ratio || bitrate <= TH.bitrate_warn_kbps || stall_ms >= TH.stall_warn_ms) {
                    status = "warn";
                }

                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    auto& wd = m_wd[name];
                    if (wd.last_status != status) {
                        wd.last_status = status;
                        send_webhook(TH, st.name, st.service_name, status, input_fps, decode_fps, bitrate, stall_ms);
                    }
                }
            }

            std::this_thread::sleep_for(300ms);
        }
    }

} // namespace multiscreen
