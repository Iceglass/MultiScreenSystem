#include "Application.h"
#include "Logger.h"
#include "FFmpegIncludes.h"
#include "StreamManager.h"
#include "WebServer.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <string>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace fs = std::filesystem;
namespace multiscreen {

    // -------- helpers --------
    static fs::path exe_dir() {
#if defined(_WIN32)
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        fs::path p = (n ? fs::path(buf) : fs::current_path());
        return p.parent_path();
#else
        return fs::current_path();
#endif
    }

    static fs::path resolve_config_dir(const std::string& name) {
        fs::path e = exe_dir();
        std::vector<fs::path> cands = {
            e / name,                 // <exe>/config
            e.parent_path() / name,   // <exe>/../config
            fs::current_path() / name // CWD/config
        };
        for (auto& c : cands) if (fs::exists(c)) return c;
        return e / name; // по умолчанию создадим здесь
    }

    static void ensure_default_configs(const fs::path& dir) {
        fs::create_directories(dir);
        const fs::path cfg = dir / "config.json";
        const fs::path streams = dir / "streams.json";

        if (!fs::exists(cfg)) {
            nlohmann::json j = {
                {"web",      {{"enable", true}, {"port", 8080}}},
                {"streams",  {{"enable", true}}}
            };
            std::ofstream(cfg) << j.dump(2);
        }

        // если streams.json нет — создадим сразу с 4 каналами
        if (!fs::exists(streams)) {
            nlohmann::json j = {
                {"streams", nlohmann::json::array({
                    { {"name","Channel_73"},  {"url","http://gr.seetv.cc/play/73/FD19D9579A8FA74/mpegts"} },
                    { {"name","Channel_86"},  {"url","http://gr.seetv.cc/play/86/FD19D9579A8FA74/mpegts"} },
                    { {"name","Channel_220"}, {"url","http://gr.seetv.cc/play/220/FD19D9579A8FA74/mpegts"} },
                    { {"name","Channel_786"}, {"url","http://gr.seetv.cc/play/786/FD19D9579A8FA74/mpegts"} }
                })}
            };
            std::ofstream(streams) << j.dump(2);
        }
    }

    // -------- Application --------
    Application::Application() = default;
    Application::~Application() { shutdown(); }

    bool Application::initialize(const std::string& configDirArg) {
        Logger::initialize("logs/app.log");
        Logger::info("Application starting...");

        fs::path cfgDir = resolve_config_dir(configDirArg);
        ensure_default_configs(cfgDir);
        Logger::info(std::string("Using config dir: ") + cfgDir.string());

        // читаем config.json
        int  web_port = 8080;
        bool web_enable = true;
        bool streams_enable = true;
        try {
            std::ifstream f(cfgDir / "config.json");
            if (f) {
                nlohmann::json j; f >> j;
                if (j.contains("web") && j["web"].contains("port"))   web_port = j["web"]["port"].get<int>();
                if (j.contains("web") && j["web"].contains("enable")) web_enable = j["web"]["enable"].get<bool>();
                if (j.contains("streams") && j["streams"].contains("enable")) streams_enable = j["streams"]["enable"].get<bool>();
            }
            else {
                Logger::warning("config.json not found; defaults will be used");
            }
        }
        catch (const std::exception& e) {
            Logger::warning(std::string("Failed to read config.json: ") + e.what());
        }

        avformat_network_init();

        m_mgr = std::make_unique<StreamManager>();

        // пробуем загрузить config/streams.json
        if (streams_enable) {
            const auto sPath = (cfgDir / "streams.json").string();
            bool ok = m_mgr->loadConfig(sPath);

            // если файл битый/пустой — подставим 4 канала, чтобы точно не было пусто
            if (!ok || m_mgr->size() == 0) {
                Logger::warning("streams.json missing/invalid or empty; using built-in 4 channels");
                m_mgr->loadFromList({
                    {"Channel_73",  "http://gr.seetv.cc/play/73/FD19D9579A8FA74/mpegts"},
                    {"Channel_86",  "http://gr.seetv.cc/play/86/FD19D9579A8FA74/mpegts"},
                    {"Channel_220", "http://gr.seetv.cc/play/220/FD19D9579A8FA74/mpegts"},
                    {"Channel_786", "http://gr.seetv.cc/play/786/FD19D9579A8FA74/mpegts"}
                    });
            }
            m_mgr->startAll();
        }
        else {
            Logger::info("Streams disabled by config");
        }

        if (web_enable) {
            m_web = std::make_unique<WebServer>(*m_mgr);
            m_web->start(web_port);
        }
        else {
            Logger::info("Web disabled by config");
        }

        Logger::info("Application initialized");
        return true;
    }

    void Application::run() {
        Logger::info("Running. Press Ctrl+C to stop (if attached).");
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    void Application::shutdown() {
        if (m_web) { m_web->stop(); m_web.reset(); }
        if (m_mgr) { m_mgr->stopAll(); m_mgr.reset(); }
        avformat_network_deinit();
        Logger::shutdown();
    }

} // namespace multiscreen
