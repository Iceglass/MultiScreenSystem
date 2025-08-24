#include "Settings.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

namespace multiscreen {

    static int clamp_int(int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    Settings& Settings::instance() {
        static Settings s;
        return s;
    }

    bool Settings::load(const std::string& path) {
        m_source_path = path;

        std::ifstream in(path);
        if (!in.is_open()) {
            return false; // оставляем дефолты
        }

        json j;
        try {
            in >> j;
        }
        catch (...) {
            return false; // оставляем дефолты
        }

        // --- UI schema ---
        if (j.contains("thresholds") && j["thresholds"].is_object()) {
            auto& jt = j["thresholds"];

            if (jt.contains("fps") && jt["fps"].is_object()) {
                auto& jf = jt["fps"];
                if (jf.contains("warn_ratio")) m_thresholds.fps.warn_ratio = std::clamp(jf["warn_ratio"].get<double>(), 0.0, 10.0);
                if (jf.contains("crit_ratio")) m_thresholds.fps.crit_ratio = std::clamp(jf["crit_ratio"].get<double>(), 0.0, 10.0);
            }

            if (jt.contains("bitrate") && jt["bitrate"].is_object()) {
                auto& jb = jt["bitrate"];
                if (jb.contains("warn_kbps")) m_thresholds.bitrate.warn_kbps = std::max(0, jb["warn_kbps"].get<int>());
                if (jb.contains("crit_kbps")) m_thresholds.bitrate.crit_kbps = std::max(0, jb["crit_kbps"].get<int>());
            }

            if (jt.contains("stall") && jt["stall"].is_object()) {
                auto& js = jt["stall"];
                if (js.contains("warn_ms")) m_thresholds.stall.warn_ms = std::max(0, js["warn_ms"].get<int>());
                if (js.contains("crit_ms")) m_thresholds.stall.crit_ms = std::max(0, js["crit_ms"].get<int>());
            }
        }

        if (j.contains("alerts") && j["alerts"].is_object()) {
            auto& ja = j["alerts"];

            // Новая схема
            if (ja.contains("webhook") && ja["webhook"].is_object()) {
                auto& jw = ja["webhook"];
                if (jw.contains("enabled"))    m_webhook.enabled = jw["enabled"].get<bool>();
                if (jw.contains("url"))        m_webhook.url = jw["url"].get<std::string>();
                if (jw.contains("timeout_ms")) m_webhook.timeout_ms = std::max(0, jw["timeout_ms"].get<int>());
            }

            // legacy: alerts.cooldown_sec
            if (ja.contains("cooldown_sec")) {
                m_webhook.cooldown_sec = std::max(0, ja["cooldown_sec"].get<int>());
            }
        }

        return true;
    }

    // --- Back-compat геттеры ---
    int Settings::decode_fps_min() const noexcept {
        try {
            std::ifstream in(m_source_path);
            if (!in.is_open()) return 0;
            json j; in >> j;
            if (j.contains("thresholds") && j["thresholds"].contains("decode_fps_min")) {
                return std::max(0, j["thresholds"]["decode_fps_min"].get<int>());
            }
        }
        catch (...) {}
        int ref_fps = 30;
        return clamp_int(static_cast<int>(ref_fps * m_thresholds.fps.warn_ratio), 0, 1000);
    }

    int Settings::bitrate_drop_pct() const noexcept {
        try {
            std::ifstream in(m_source_path);
            if (!in.is_open()) return 0;
            json j; in >> j;
            if (j.contains("thresholds") && j["thresholds"].contains("bitrate_drop_pct")) {
                return clamp_int(j["thresholds"]["bitrate_drop_pct"].get<int>(), 0, 100);
            }
        }
        catch (...) {}
        return 0;
    }

    int Settings::cc_errors_per_min() const noexcept {
        try {
            std::ifstream in(m_source_path);
            if (!in.is_open()) return 0;
            json j; in >> j;
            if (j.contains("thresholds") && j["thresholds"].contains("cc_errors_per_min")) {
                return std::max(0, j["thresholds"]["cc_errors_per_min"].get<int>());
            }
        }
        catch (...) {}
        return 0;
    }

} // namespace multiscreen
