#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace multiscreen {

    // —обираемые метрики/пол€ Ц используютс€ WebServer'ом
    struct StreamStats {
        std::string name;
        std::string url;

        bool   running = false;
        double input_fps = 0.0;   // по stream->avg_frame_rate/r_frame_rate
        double decode_fps = 0.0;   // сглаженный EWMA
        double render_fps = 0.0;   // пока 0 (если нет отрисовки)

        int bitrate_kbps = 0;     // общий kbps
        int v_kbps = 0;     // видео kbps
        int a_kbps = 0;     // аудио kbps

        std::string rate_mode;       // "VBR"/"CBR" (по вариативности kbps)
        std::string decoder;         // "CPU" или "GPU(D3D11VA)" и т.п.

        uint64_t cc_errors = 0;     // если считаете TEI/CC Ч сюда

        // PSI / PID Ц ƒ≈—я“»„Ќџ≈
        int sid = -1;
        int pmt_pid = -1;
        int pcr_pid = -1;
        int video_pid = -1;
        std::vector<int> audio_pids;
        std::string service_name;

        std::string last_error;
        std::string status;          // ok/warn/crit Ц дл€ watchdog
        std::string status_reason;
    };

    class Stream {
    public:
        Stream(const std::string& name, const std::string& url);
        ~Stream();

        void start();
        void stop();

        StreamStats stats(); // потокобезопасно

    private:
        // --- базова€ информаци€ ---
        std::string m_name;
        std::string m_url;

        // --- ffmpeg объекты ---
        AVFormatContext* m_fmt = nullptr;
        AVCodecContext* m_vdec = nullptr;   // видео-декодер (дл€ подсчЄта кадров)
        int                m_vst_index = -1;

        // --- поток/синхронизаци€ ---
        std::thread        m_thr;
        std::atomic<bool>  m_run{ false };
        std::mutex         m_mx;     // защищает пол€ статистики/состо€ни€

        // --- счЄтчики/метрики ---
        // битрейт (за 1с окно)
        std::chrono::steady_clock::time_point m_bw_t0{};
        uint64_t m_bw_bits_total = 0;
        int      m_kbps = 0, m_vkbps = 0, m_akbps = 0;

        // режим VBR/CBR Ц проста€ эвристика по дисперсии kbps
        int      m_kbps_win_sum = 0;
        int      m_kbps_win_cnt = 0;
        std::string m_rate_mode = "VBR";

        // decoder label
        std::string m_decoder_label = "CPU";

        // ошибки CC (если парсите TS) Ц здесь инкрементируйте
        uint64_t m_cc_errors = 0;

        // PSI/PID Ц „»—Ћј
        int m_sid = -1;
        int m_pmt_pid = -1;
        int m_pcr_pid = -1;
        int m_video_pid = -1;
        std::vector<int> m_audio_pids;
        std::string m_service_name;

        // fps: входной и сглаженный decode
        double m_input_fps_hint = 0.0;
        std::chrono::steady_clock::time_point m_dec_sample_t0{};
        int    m_dec_sample_frames = 0;
        double m_dec_fps_ema = 0.0;

        // render fps (если по€витс€ рендер) Ц оставлено
        double m_render_fps = 0.0;

        std::string m_last_error;

    private:
        void thread_loop();
        bool open_input();
        void close_input();

        void pick_input_fps(AVStream* st);
        void on_video_frame_decoded();
        void update_bitrate_window(int pkt_bits, bool is_video, bool is_audio);

        void probe_program_info(); // заполн€ет SID/PMT/PCR/ES PID и service_name (если доступно)
    };

} // namespace multiscreen
