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

    // ���������� �������/���� � ������������ WebServer'��
    struct StreamStats {
        std::string name;
        std::string url;

        bool   running = false;
        double input_fps = 0.0;   // �� stream->avg_frame_rate/r_frame_rate
        double decode_fps = 0.0;   // ���������� EWMA
        double render_fps = 0.0;   // ���� 0 (���� ��� ���������)

        int bitrate_kbps = 0;     // ����� kbps
        int v_kbps = 0;     // ����� kbps
        int a_kbps = 0;     // ����� kbps

        std::string rate_mode;       // "VBR"/"CBR" (�� ������������� kbps)
        std::string decoder;         // "CPU" ��� "GPU(D3D11VA)" � �.�.

        uint64_t cc_errors = 0;     // ���� �������� TEI/CC � ����

        // PSI / PID � ����������
        int sid = -1;
        int pmt_pid = -1;
        int pcr_pid = -1;
        int video_pid = -1;
        std::vector<int> audio_pids;
        std::string service_name;

        std::string last_error;
        std::string status;          // ok/warn/crit � ��� watchdog
        std::string status_reason;
    };

    class Stream {
    public:
        Stream(const std::string& name, const std::string& url);
        ~Stream();

        void start();
        void stop();

        StreamStats stats(); // ���������������

    private:
        // --- ������� ���������� ---
        std::string m_name;
        std::string m_url;

        // --- ffmpeg ������� ---
        AVFormatContext* m_fmt = nullptr;
        AVCodecContext* m_vdec = nullptr;   // �����-������� (��� �������� ������)
        int                m_vst_index = -1;

        // --- �����/������������� ---
        std::thread        m_thr;
        std::atomic<bool>  m_run{ false };
        std::mutex         m_mx;     // �������� ���� ����������/���������

        // --- ��������/������� ---
        // ������� (�� 1� ����)
        std::chrono::steady_clock::time_point m_bw_t0{};
        uint64_t m_bw_bits_total = 0;
        int      m_kbps = 0, m_vkbps = 0, m_akbps = 0;

        // ����� VBR/CBR � ������� ��������� �� ��������� kbps
        int      m_kbps_win_sum = 0;
        int      m_kbps_win_cnt = 0;
        std::string m_rate_mode = "VBR";

        // decoder label
        std::string m_decoder_label = "CPU";

        // ������ CC (���� ������� TS) � ����� ���������������
        uint64_t m_cc_errors = 0;

        // PSI/PID � �����
        int m_sid = -1;
        int m_pmt_pid = -1;
        int m_pcr_pid = -1;
        int m_video_pid = -1;
        std::vector<int> m_audio_pids;
        std::string m_service_name;

        // fps: ������� � ���������� decode
        double m_input_fps_hint = 0.0;
        std::chrono::steady_clock::time_point m_dec_sample_t0{};
        int    m_dec_sample_frames = 0;
        double m_dec_fps_ema = 0.0;

        // render fps (���� �������� ������) � ���������
        double m_render_fps = 0.0;

        std::string m_last_error;

    private:
        void thread_loop();
        bool open_input();
        void close_input();

        void pick_input_fps(AVStream* st);
        void on_video_frame_decoded();
        void update_bitrate_window(int pkt_bits, bool is_video, bool is_audio);

        void probe_program_info(); // ��������� SID/PMT/PCR/ES PID � service_name (���� ��������)
    };

} // namespace multiscreen
