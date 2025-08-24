#include "Metrics.h"
#include "Settings.h"
#include "Alerts.h"

#include <chrono>
#include <algorithm>

using namespace std::chrono;
using alerts::Severity;

namespace multiscreen {

    // ===== statics =====
    Metrics& Metrics::instance() {
        static Metrics g;
        return g;
    }

    int64_t Metrics::nowMsSteady() noexcept {
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    // ===== public API =====
    void Metrics::setExpectedFps(int fps) noexcept {
        if (fps < 1) fps = 1;
        if (fps > 300) fps = 300;
        m_expected_fps_.store(fps, std::memory_order_relaxed);
    }

    void Metrics::onFrameRendered(int64_t /*pts_ms*/) noexcept {
        const int64_t now = nowMsSteady();
        {
            std::lock_guard<std::mutex> lk(m_fps_mu_);
            m_frame_times_ms_.push_back(now);
            const int64_t horizon = now - 10000; // ~10s tail
            while (!m_frame_times_ms_.empty() && m_frame_times_ms_.front() < horizon) {
                m_frame_times_ms_.pop_front();
            }
        }
        m_last_progress_ms_.store(now, std::memory_order_relaxed);
    }

    void Metrics::onBytesReceived(size_t /*bytes*/) noexcept {
        m_last_progress_ms_.store(nowMsSteady(), std::memory_order_relaxed);
    }

    void Metrics::onPacketTs(const uint8_t* pkt, size_t len) noexcept {
        handleTsPacket(pkt, len);
    }

    // === getters ===
    double Metrics::renderFps(double window_sec) const noexcept {
        if (window_sec <= 0.1) window_sec = 0.1;
        const int64_t now = nowMsSteady();
        const int64_t window_ms = static_cast<int64_t>(window_sec * 1000.0);

        std::lock_guard<std::mutex> lk(m_fps_mu_);
        auto it = std::lower_bound(m_frame_times_ms_.begin(), m_frame_times_ms_.end(),
            now - window_ms);
        const size_t count = static_cast<size_t>(std::distance(it, m_frame_times_ms_.end()));
        return static_cast<double>(count) / window_sec;
    }

    int Metrics::ccErrorsPerMin() const noexcept {
        const int64_t now = nowMsSteady();
        const int64_t horizon = now - 60000;

        std::lock_guard<std::mutex> lk(m_cc_mu_);
        while (!m_cc_err_times_ms_.empty() && m_cc_err_times_ms_.front() < horizon) {
            m_cc_err_times_ms_.pop_front();
        }
        return static_cast<int>(m_cc_err_times_ms_.size());
    }

    int64_t Metrics::stallMsNow() const noexcept {
        const int64_t last_prog_ms = m_last_progress_ms_.load(std::memory_order_relaxed);
        if (last_prog_ms <= 0) return 0;
        const int64_t now = nowMsSteady();
        return (now > last_prog_ms) ? (now - last_prog_ms) : 0;
    }

    // ===== TS continuity handling =====
    void Metrics::handleTsPacket(const uint8_t* pkt, size_t len) noexcept {
        uint16_t pid = 0;
        bool payload = false;
        bool discontinuity = false;
        uint8_t cc = 0;
        if (!tsParseHeader(pkt, len, pid, payload, discontinuity, cc)) return;

        std::lock_guard<std::mutex> lk(m_cc_mu_);

        auto& st = m_cc_map_[pid];
        if (discontinuity) {
            st.valid = false; // сброс ожиданий CC
            return;
        }

        if (!st.valid) {
            st.valid = true;
            st.last_cc = cc;
            return;
        }

        if (payload) {
            const uint8_t expected = static_cast<uint8_t>((st.last_cc + 1) & 0x0F);
            if (cc != expected) {
                noteCcError();
                // продолжаем с текущим значением
            }
            st.last_cc = cc;
        }
    }

    bool Metrics::tsParseHeader(const uint8_t* pkt, size_t len,
        uint16_t& pid, bool& payload_present,
        bool& discontinuity, uint8_t& cc) noexcept
    {
        if (!pkt || len < 188) return false;
        if (pkt[0] != 0x47) return false;

        pid = static_cast<uint16_t>(((pkt[1] & 0x1F) << 8) | pkt[2]);
        const uint8_t afc = static_cast<uint8_t>((pkt[3] >> 4) & 0x03);
        cc = static_cast<uint8_t>(pkt[3] & 0x0F);

        const bool adaptation = (afc == 2) || (afc == 3);
        payload_present = (afc == 1) || (afc == 3);
        discontinuity = false;

        if (adaptation) {
            size_t off = 4;
            if (off >= len) return true;
            const uint8_t afl = pkt[off++];
            if (afl > 0 && off + afl <= len) {
                const uint8_t flags = pkt[off];
                discontinuity = ((flags & 0x80) != 0); // bit7
            }
        }
        return true;
    }

    void Metrics::noteCcError() noexcept {
        const int64_t now = nowMsSteady();
        m_cc_err_times_ms_.push_back(now);

        const int64_t horizon = now - 60000;
        while (!m_cc_err_times_ms_.empty() && m_cc_err_times_ms_.front() < horizon) {
            m_cc_err_times_ms_.pop_front();
        }
    }

    void Metrics::gcWindows_() noexcept {
        const int64_t now = nowMsSteady();
        {
            std::lock_guard<std::mutex> lk(m_fps_mu_);
            const int64_t horizon = now - 10000;
            while (!m_frame_times_ms_.empty() && m_frame_times_ms_.front() < horizon) {
                m_frame_times_ms_.pop_front();
            }
        }
        {
            std::lock_guard<std::mutex> lk(m_cc_mu_);
            const int64_t horizon = now - 60000;
            while (!m_cc_err_times_ms_.empty() && m_cc_err_times_ms_.front() < horizon) {
                m_cc_err_times_ms_.pop_front();
            }
        }
    }

    // ===== polling & alerts =====
    void Metrics::pollAndAlert(const char* name) {
        gcWindows_();

        const auto& s = Settings::instance();
        const auto& th = s.thresholds();

        // Stall
        {
            const int64_t stall = stallMsNow();
            Severity sev = Severity::Info;
            bool fire = false;

            if (stall >= th.stall.crit_ms) { sev = Severity::Critical; fire = true; }
            else if (stall >= th.stall.warn_ms) { sev = Severity::Warning; fire = true; }

            if (fire) {
                char title[128];
                snprintf(title, sizeof(title), "%s: Stall %lld ms",
                    (name ? name : "Stream"),
                    static_cast<long long>(stall));
                alerts::send_webhook(title, "No progress detected", sev);
            }
        }

        // FPS
        {
            const int expected = m_expected_fps_.load(std::memory_order_relaxed);
            const double fps = renderFps(2.0);
            Severity sev = Severity::Info;
            bool fire = false;

            if (fps <= th.fps.crit_ratio * expected) { sev = Severity::Critical; fire = true; }
            else if (fps <= th.fps.warn_ratio * expected) { sev = Severity::Warning; fire = true; }

            if (fire) {
                char title[128];
                snprintf(title, sizeof(title), "%s: Low FPS %.1f (exp %d)",
                    (name ? name : "Stream"), fps, expected);
                alerts::send_webhook(title, "Render FPS below threshold", sev);
            }
        }

        // CC errors/min (legacy порог, если задан)
        {
            const int per_min = ccErrorsPerMin();
            const int legacy_cc_limit = s.cc_errors_per_min(); // 0 = нет порога

            if (legacy_cc_limit > 0 && per_min >= legacy_cc_limit) {
                char title[128];
                snprintf(title, sizeof(title), "%s: CC errors/min = %d (>= %d)",
                    (name ? name : "Stream"), per_min, legacy_cc_limit);
                alerts::send_webhook(title, "Transport continuity errors", Severity::Warning);
            }
        }
    }

} // namespace multiscreen
