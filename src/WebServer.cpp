#include "WebServer.h"
#include "StreamManager.h"
#include "Logger.h"
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <fstream>
#include <sstream>
#include <iterator>
#include <string>
#include <vector>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace multiscreen {
    namespace {
        bool read_text_file(const fs::path& p, std::string& out) {
            try {
                std::ifstream f(p, std::ios::binary);
                if (!f) return false;
                out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
                return true;
            }
            catch (...) { return false; }
        }
    } // anonymous

    WebServer::WebServer(StreamManager& mgr) : m_mgr(mgr) {}
    WebServer::~WebServer() { stop(); }

    bool WebServer::start(int port) {
        if (m_running.load()) return true;
        m_port = port;
        m_svr = std::make_unique<httplib::Server>();

        // Отдача статики из папки www включая css, js и другие
        m_svr->set_mount_point("/", "www");

        // Обработка favicon.ico — возвращаем 204 No Content, чтобы не было 404
        m_svr->Get("/favicon.ico", [](const httplib::Request&, httplib::Response& res) {
            res.status = 204;
            res.set_content("", "image/x-icon");
            });

        // Главная страница
        m_svr->Get("/", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(WebServer::load_index_html_from_disk(), "text/html; charset=utf-8");
            });

        // Settings endpoint
        m_svr->Get("/api/settings", [](const httplib::Request&, httplib::Response& res) {
            try {
                std::ifstream f("config/settings.json", std::ios::binary);
                if (f) {
                    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    res.set_content(body, "application/json");
                    return;
                }
            }
            catch (...) {}
            json j = {
                {"thresholds", {
                    {"fps", {{"warn_ratio", 0.70}, {"crit_ratio", 0.40}}},
                    {"bitrate", {{"warn_kbps", 300}, {"crit_kbps", 100}}},
                    {"stall", {{"warn_ms", 3000}, {"crit_ms", 7000}}}
                }},
                {"alerts", {{"webhook", {{"enabled", false}, {"url", ""}, {"timeout_ms", 1500}}}}}
            };
            res.set_content(j.dump(), "application/json");
            });

        // Расширенный список потоков (совместимо со старым UI)
        m_svr->Get("/api/streams", [this](const httplib::Request&, httplib::Response& res) {
            auto vec = m_mgr.getAllStats();
            json j = json::array();
            for (const auto& s : vec) {
                json r = json::object();
                r["name"] = s.name;
                r["url"] = s.url;
                r["running"] = s.running;
                r["input_fps"] = s.input_fps;
                r["decode_fps"] = s.decode_fps;
                r["render_fps"] = s.render_fps;
                r["bitrate_kbps"] = s.bitrate_kbps;
                r["video_kbps"] = s.v_kbps;
                r["audio_kbps"] = s.a_kbps;
                r["rate_mode"] = s.rate_mode;
                r["cc_errors"] = s.cc_errors;
                r["decoder"] = s.decoder;
                r["sid"] = s.sid;
                r["pmt_pid"] = s.pmt_pid;
                r["pcr_pid"] = s.pcr_pid;
                r["video_pid"] = s.video_pid;
                r["audio_pids"] = s.audio_pids;
                r["service_name"] = s.service_name;
                r["last_error"] = s.last_error;
                r["status"] = s.status;
                j.push_back(std::move(r));
            }
            res.set_content(j.dump(), "application/json; charset=utf-8");
            });

        // Методы POST для управления
        m_svr->Post("/api/stream/start", [this](const httplib::Request& req, httplib::Response& res) {
            auto j = WebServer::parse_json(req.body);
            res.set_content(json{ {"ok", m_mgr.startStream(j.value("name", std::string()))} }.dump(), "application/json");
            });
        m_svr->Post("/api/stream/stop", [this](const httplib::Request& req, httplib::Response& res) {
            auto j = WebServer::parse_json(req.body);
            res.set_content(json{ {"ok", m_mgr.stopStream(j.value("name", std::string()))} }.dump(), "application/json");
            });
        m_svr->Post("/api/stream/restart", [this](const httplib::Request& req, httplib::Response& res) {
            auto j = WebServer::parse_json(req.body);
            res.set_content(json{ {"ok", m_mgr.restartStream(j.value("name", std::string()))} }.dump(), "application/json");
            });
        m_svr->Post("/api/stream/delete", [this](const httplib::Request& req, httplib::Response& res) {
            auto j = WebServer::parse_json(req.body);
            bool ok = false;
            auto name = j.value("name", std::string());
            if (!name.empty()) {
                ok = m_mgr.removeStream(name);
                if (ok) WebServer::persist_remove_stream(name);
            }
            res.set_content(json{ {"ok", ok} }.dump(), "application/json");
            });
        m_svr->Post("/api/stream/add", [this](const httplib::Request& req, httplib::Response& res) {
            auto j = WebServer::parse_json(req.body);
            bool ok = false;
            auto n = j.value("name", std::string());
            auto u = j.value("url", std::string());
            if (!n.empty() && !u.empty()) {
                ok = m_mgr.addStream(n, u);
                if (ok) WebServer::persist_append_stream(n, u);
            }
            res.set_content(json{ {"ok", ok} }.dump(), "application/json");
            });

        // Старые маршруты для совместимости
        m_svr->Post(R"(/api/streams/(.+)/start)", [this](const httplib::Request& req, httplib::Response& res) {
            if (req.matches.size() < 2) {
                res.status = 400;
                res.set_content("{}", "application/json");
                return;
            }
            const std::string name = req.matches[1].str();
            res.set_content(json{ {"ok", m_mgr.startStream(name)} }.dump(), "application/json");
            });
        m_svr->Post(R"(/api/streams/(.+)/stop)", [this](const httplib::Request& req, httplib::Response& res) {
            if (req.matches.size() < 2) {
                res.status = 400;
                res.set_content("{}", "application/json");
                return;
            }
            const std::string name = req.matches[1].str();
            res.set_content(json{ {"ok", m_mgr.stopStream(name)} }.dump(), "application/json");
            });
        m_svr->Post(R"(/api/streams/(.+)/restart)", [this](const httplib::Request& req, httplib::Response& res) {
            if (req.matches.size() < 2) {
                res.status = 400;
                res.set_content("{}", "application/json");
                return;
            }
            const std::string name = req.matches[1].str();
            res.set_content(json{ {"ok", m_mgr.restartStream(name)} }.dump(), "application/json");
            });
        m_svr->Delete(R"(/api/streams/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
            if (req.matches.size() < 2) {
                res.status = 400;
                res.set_content("{}", "application/json");
                return;
            }
            const std::string name = req.matches[1].str();
            bool ok = m_mgr.removeStream(name);
            if (ok) WebServer::persist_remove_stream(name);
            res.set_content(json{ {"ok", ok} }.dump(), "application/json");
            });
        m_svr->Post("/api/streams", [this](const httplib::Request& req, httplib::Response& res) {
            auto body = WebServer::parse_json(req.body);
            auto n = body.value("name", std::string());
            auto u = body.value("url", std::string());
            bool ok = false;
            if (!n.empty() && !u.empty()) {
                ok = m_mgr.addStream(n, u);
                if (ok) WebServer::persist_append_stream(n, u);
            }
            res.set_content(json{ {"ok", ok} }.dump(), "application/json");
            });

        m_svr->set_default_headers({
            {"Cache-Control", "no-store"},
            {"Access-Control-Allow-Origin", "*"}
            });

        m_running = true;

        m_thread = std::thread([this] {
            Logger::info("WebServer listening on port " + std::to_string(m_port));
            m_svr->listen("0.0.0.0", m_port);
            Logger::info("WebServer stopped");
            m_running = false;
            });
        return true;
    }

    void WebServer::stop() {
        if (!m_running.load()) return;
        m_svr->stop();
        if (m_thread.joinable()) m_thread.join();
        m_running = false;
    }

    nlohmann::json WebServer::parse_json(const std::string& body) {
        try {
            if (!body.empty()) return nlohmann::json::parse(body);
        }
        catch (...) {}
        return nlohmann::json::object();
    }

    void WebServer::persist_append_stream(const std::string& name, const std::string& url) {
        try {
            json root = json::array();
            {
                std::ifstream in("config/streams.json", std::ios::binary);
                if (in) in >> root;
            }
            auto upsert = [&](json& arr) {
                bool updated = false;
                for (auto& it : arr) {
                    if (it.is_object() && it.value("name", std::string()) == name) {
                        it["url"] = url;
                        updated = true;
                        break;
                    }
                }
                if (!updated) arr.push_back(json{ {"name", name}, {"url", url} });
                };
            if (root.is_array()) upsert(root);
            else if (root.is_object()) {
                if (!root.contains("streams") || !root["streams"].is_array())
                    root["streams"] = json::array();
                upsert(root["streams"]);
            }
            else {
                root = json::array({ json{{"name", name}, {"url", url}} });
            }
            std::ofstream out("config/streams.json", std::ios::binary | std::ios::trunc);
            out << root.dump(2);
        }
        catch (...) {}
    }

    void WebServer::persist_remove_stream(const std::string& name) {
        try {
            json root = json::array();
            {
                std::ifstream in("config/streams.json", std::ios::binary);
                if (in) in >> root;
            }
            auto remove_from = [&](json& arr) {
                json out = json::array();
                for (const auto& it : arr) {
                    if (it.is_object() && it.value("name", std::string()) == name) continue;
                    out.push_back(it);
                }
                arr = std::move(out);
                };
            if (root.is_array())
                remove_from(root);
            else if (root.is_object()) {
                if (root.contains("streams") && root["streams"].is_array())
                    remove_from(root["streams"]);
            }
            std::ofstream out("config/streams.json", std::ios::binary | std::ios::trunc);
            out << root.dump(2);
        }
        catch (...) {}
    }

    std::string WebServer::load_index_html_from_disk() {
        const char* kRel = "www/index.html";
        std::vector<fs::path> candidates = {
            fs::path(kRel),
            fs::path("..") / kRel,
            fs::path("..") / ".." / kRel
        };
        std::string html;
        for (const auto& p : candidates) {
            if (read_text_file(p, html)) return html;
        }
        return "<!doctype html><html><head><meta charset=\"utf-8\"><title>MultiScreen</title></head>"
            "<body><p>Place www/index.html</p></body></html>";
    }

} // namespace multiscreen
