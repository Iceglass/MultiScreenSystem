#pragma once
#include <cstdint>
#include <cstddef>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace multiscreen {

    // ������������� ������� ��� ������ ��������/��������.
    // ��������� �� ����� ������������:
    //  - onFrameRendered(pts_ms)    � ����� ���� ������� (��� FPS � stall)
    //  - onPacketTs(pkt, len)       � ��� ������� TS-������ 188 ���� (��� CC errors)
    //  - onBytesReceived(bytes)     � ��� ������� ��������� (��� stall)
    //  - pollAndAlert(name)         � ������������� �������� ������� � �������� �������
    //
    // � UI ������ ������� ������� renderFps(), ccErrorsPerMin(), stallMsNow().
    class Metrics {
    public:
        static Metrics& instance();

        // --- �������������� ������ ---
        void setExpectedFps(int fps) noexcept;
        void onFrameRendered(int64_t pts_ms) noexcept;
        void onBytesReceived(size_t bytes) noexcept;
        void onPacketTs(const uint8_t* pkt, size_t len) noexcept;

        // --- ������������� ����� � ������ ---
        void pollAndAlert(const char* name);

        // --- ������� ��� UI/REST ---
        double  renderFps(double window_sec = 2.0) const noexcept;
        int     ccErrorsPerMin() const noexcept;
        int64_t stallMsNow() const noexcept;

    private:
        Metrics() = default;

        // ====== Render FPS ======
        mutable std::mutex m_fps_mu_;
        std::deque<int64_t> m_frame_times_ms_;
        std::atomic<int> m_expected_fps_{ 30 };

        // ====== Stall ======
        std::atomic<int64_t> m_last_progress_ms_{ 0 };

        // ====== CC errors ======
        struct CcState {
            bool     valid = false;
            uint8_t  last_cc = 0;
        };
        mutable std::mutex m_cc_mu_;
        std::unordered_map<uint16_t, CcState> m_cc_map_;
        // �������������� � const-������ ccErrorsPerMin() � �������� mutable
        mutable std::deque<int64_t> m_cc_err_times_ms_;

        // ====== helpers ======
        static int64_t nowMsSteady() noexcept;

        void handleTsPacket(const uint8_t* pkt, size_t len) noexcept;
        static bool tsParseHeader(const uint8_t* pkt, size_t len,
            uint16_t& pid, bool& payload_present,
            bool& discontinuity, uint8_t& cc) noexcept;

        void noteCcError() noexcept;
        void gcWindows_() noexcept;
    };

} // namespace multiscreen
