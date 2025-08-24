#pragma once
#include <string>
#include <cstdint>

namespace alerts {

    enum class Severity {
        Info = 0,
        Warning = 1,
        Critical = 2
    };

    // Единый способ отправки вебхуков
    bool send_webhook(const std::string& title,
        const std::string& message,
        Severity level,
        int64_t now_ms = -1);

    // На отладку можно переопределить cooldown
    void set_cooldown_override(int seconds);

} // namespace alerts

// --- Совместимость со старым кодом ---
namespace multiscreen {

    class Alerts {
    public:
        // Старый вызов без уровня → считаем info
        static bool post_webhook(const std::string& title,
            const std::string& message)
        {
            return alerts::send_webhook(title, message, alerts::Severity::Info);
        }

        // Старый вызов с int уровнем
        static bool post_webhook(const std::string& title,
            const std::string& message,
            int level)
        {
            alerts::Severity s = alerts::Severity::Info;
            if (level >= 2)      s = alerts::Severity::Critical;
            else if (level == 1) s = alerts::Severity::Warning;
            return alerts::send_webhook(title, message, s);
        }

        static void set_cooldown_override(int seconds) {
            alerts::set_cooldown_override(seconds);
        }
    };

} // namespace multiscreen
