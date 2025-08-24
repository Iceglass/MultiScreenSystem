#pragma once
#include <string>
#include <cstdint>

namespace multiscreen {

    struct Settings {
        // Singleton
        static Settings& instance();

        // �������� UI-����� �� config/settings.json
        // ���������� true ��� ������ (��� ������ � �������� �������).
        bool load(const std::string& path);

        // --- ������ (UI) ����� �������� ---
        struct Thresholds {
            struct FPS { double warn_ratio = 0.75; double crit_ratio = 0.50; } fps;
            struct Bitrate { int    warn_kbps = 1500; int    crit_kbps = 500; } bitrate;
            struct Stall { int    warn_ms = 1000; int    crit_ms = 3000; } stall;
        };

        struct Webhook {
            bool        enabled = false;
            std::string url;
            int         timeout_ms = 2000;
            int         cooldown_sec = 60; // ������������ cooldown ��� �������������
        };

        const Thresholds& thresholds() const noexcept { return m_thresholds; }
        const Webhook& webhook()   const noexcept { return m_webhook; }

        // --- Back-compat ������� ��� ������� ���� ---
        int decode_fps_min()    const noexcept;   // legacy: thresholds.decode_fps_min
        int bitrate_drop_pct()  const noexcept;   // legacy: thresholds.bitrate_drop_pct
        int cc_errors_per_min() const noexcept;   // legacy: thresholds.cc_errors_per_min

        // ��� ������ ������� ����� Settings::alerts_webhook_url()
        const std::string& alerts_webhook_url() const noexcept { return m_webhook.url; }
        int alerts_cooldown_sec() const noexcept { return m_webhook.cooldown_sec; }

        // �����������
        const std::string& source_path() const noexcept { return m_source_path; }

    private:
        Settings() = default;

        Thresholds  m_thresholds{};
        Webhook     m_webhook{};
        std::string m_source_path;
    };

} // namespace multiscreen
