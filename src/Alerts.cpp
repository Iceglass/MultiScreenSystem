#include "Alerts.h"
#include "Settings.h"
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <chrono>
#include <unordered_map>
#include <mutex>

using json = nlohmann::json;

namespace alerts {
    namespace {
        std::mutex g_mu;
        int g_cooldown_override_sec = -1; // <0 = не переопределён
        std::unordered_map<std::string, int64_t> g_last_sent_ms;

        static int64_t now_ms_steady() {
            using namespace std::chrono;
            return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        }

        static std::string make_key(const std::string& title, Severity s) {
            return title + "#" + std::to_string(static_cast<int>(s));
        }

        // Подавляем предупреждение C4505 с помощью maybe_unused
        [[maybe_unused]] static bool is_https(const std::string& url) {
            return url.rfind("https://", 0) == 0;
        }

        static bool is_http(const std::string& url) {
            return url.rfind("http://", 0) == 0;
        }
    } // namespace

    void set_cooldown_override(int seconds) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_cooldown_override_sec = seconds;
    }

    bool send_webhook(const std::string& title,
        const std::string& message,
        Severity level,
        int64_t now_ms)
    {
        const auto& s = multiscreen::Settings::instance();
        const auto& wh = s.webhook();
        if (!wh.enabled || wh.url.empty())
            return true;

        const int cooldown = (g_cooldown_override_sec >= 0) ? g_cooldown_override_sec
            : wh.cooldown_sec;
        const int64_t tnow = (now_ms >= 0) ? now_ms : now_ms_steady();
        const std::string key = make_key(title, level);

        {
            std::lock_guard<std::mutex> lk(g_mu);
            auto it = g_last_sent_ms.find(key);
            if (it != g_last_sent_ms.end()) {
                const int64_t delta_ms = tnow - it->second;
                if (delta_ms < static_cast<int64_t>(cooldown) * 1000) {
                    return true; // под кулдауном — пропускаем
                }
            }
            g_last_sent_ms[key] = tnow;
        }

        // JSON полезная нагрузка
        json body = {
            {"title",   title},
            {"message", message},
            {"severity", (level == Severity::Info ? "info" :
                         (level == Severity::Warning ? "warning" : "critical"))},
            {"source",  "MultiScreenSystem"}
        };

        // Простейший разбор URL для httplib (host/port/path)
        const std::string& url = wh.url;
        std::string host, path;
        int port = -1;
        auto split = [&](const std::string& u) {
            const std::string prot_sep = "://";
            auto ppos = u.find(prot_sep);
            if (ppos == std::string::npos) return;
            auto rest = u.substr(ppos + prot_sep.size());
            auto slash = rest.find('/');
            std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
            path = (slash == std::string::npos) ? "/" : rest.substr(slash);
            auto colon = hostport.find(':');
            if (colon == std::string::npos) {
                host = hostport;
            }
            else {
                host = hostport.substr(0, colon);
                try { port = std::stoi(hostport.substr(colon + 1)); }
                catch (...) { port = -1; }
            }
            };
        split(url);

        if (host.empty()) {
            return false;
        }

        bool ok = false;
        const auto timeout = std::chrono::milliseconds((wh.timeout_ms > 0) ? wh.timeout_ms : 2000);

        if (is_http(url)) {
            httplib::Client cli(host.c_str(), (port > 0 ? port : 80));
            cli.set_connection_timeout(timeout);
            cli.set_read_timeout(timeout);
            cli.set_write_timeout(timeout);
            auto res = cli.Post(path.c_str(), body.dump(), "application/json");
            ok = (res && res->status >= 200 && res->status < 300);
        }
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        else if (is_https(url)) {
            httplib::SSLClient cli(host.c_str(), (port > 0 ? port : 443));
            cli.set_connection_timeout(timeout);
            cli.set_read_timeout(timeout);
            cli.set_write_timeout(timeout);
            auto res = cli.Post(path.c_str(), body.dump(), "application/json");
            ok = (res && res->status >= 200 && res->status < 300);
        }
#endif
        else {
            ok = false;
        }

        return ok;
    }
} // namespace alerts
