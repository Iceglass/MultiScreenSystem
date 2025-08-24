#include "Stream.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/avstring.h>
#include <libavutil/rational.h>
}

namespace multiscreen {

    Stream::Stream(const std::string& name, const std::string& url)
        : m_name(name), m_url(url) {
    }

    Stream::~Stream() { stop(); }

    void Stream::start() {
        if (m_run.load()) return;
        m_run = true;
        m_thr = std::thread(&Stream::thread_loop, this);
    }

    void Stream::stop() {
        m_run = false;
        if (m_thr.joinable()) m_thr.join();
        close_input();
    }

    StreamStats Stream::stats() {
        std::lock_guard<std::mutex> lk(m_mx);
        StreamStats st;
        st.name = m_name;
        st.url = m_url;

        st.running = m_run.load();
        st.input_fps = m_input_fps_hint;

        // ---- Decode FPS: EWMA, затем ∆®—“ »… CLAMP к input_fps ----
        double dfps = m_dec_fps_ema;
        if (m_input_fps_hint > 0.0) {
            if (dfps > m_input_fps_hint) dfps = m_input_fps_hint;
            if (dfps <= 0.0)           dfps = m_input_fps_hint; // fallback
        }
        else {
            // входной не известен Ч покажем то, что насчитано
            if (dfps < 0.0) dfps = 0.0;
        }
        st.decode_fps = dfps;

        st.render_fps = m_render_fps;

        st.bitrate_kbps = m_kbps;
        st.v_kbps = m_vkbps;
        st.a_kbps = m_akbps;

        st.rate_mode = m_rate_mode;
        st.decoder = m_decoder_label;
        st.cc_errors = m_cc_errors;

        st.sid = m_sid;
        st.pmt_pid = m_pmt_pid;
        st.pcr_pid = m_pcr_pid;
        st.video_pid = m_video_pid;
        st.audio_pids = m_audio_pids;
        st.service_name = m_service_name;

        st.last_error = m_last_error;
        return st;
    }

    void Stream::thread_loop() {
        while (m_run.load()) {
            if (!open_input()) {
                { std::lock_guard<std::mutex> lk(m_mx); m_last_error = "open failed"; }
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            AVPacket* pkt = av_packet_alloc();
            AVFrame* frm = av_frame_alloc();
            if (!pkt || !frm) {
                { std::lock_guard<std::mutex> lk(m_mx); m_last_error = "no mem"; }
                av_packet_free(&pkt); av_frame_free(&frm);
                close_input();
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            // окна дл€ kbps
            {
                std::lock_guard<std::mutex> lk(m_mx);
                m_bw_t0 = std::chrono::steady_clock::now();
                m_bw_bits_total = 0;
                m_kbps = m_vkbps = m_akbps = 0;
                m_kbps_win_sum = m_kbps_win_cnt = 0;
            }

            // декод-цикл
            while (m_run.load()) {
                int r = av_read_frame(m_fmt, pkt);
                if (r < 0) {
                    if (r == AVERROR_EOF) break;
                    // сетевые ошибки/таймаут Ч реконнект
                    break;
                }

                // учЄт битрейта (вход€щие пакеты Ц общий kbps)
                bool is_video = (pkt->stream_index == m_vst_index);
                bool is_audio = (!is_video && m_vst_index >= 0 && m_fmt &&
                    pkt->stream_index >= 0 &&
                    m_fmt->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO);
                update_bitrate_window(pkt->size * 8, is_video, is_audio);

                // отправим видеопакеты в декодер ради счЄтчика кадров
                if (is_video && m_vdec) {
                    if (avcodec_send_packet(m_vdec, pkt) == 0) {
                        while (m_run.load()) {
                            int rr = avcodec_receive_frame(m_vdec, frm);
                            if (rr == 0) {
                                on_video_frame_decoded();
                                av_frame_unref(frm);
                            }
                            else if (rr == AVERROR(EAGAIN) || rr == AVERROR_EOF) {
                                break;
                            }
                            else {
                                // ошибка декодера Ц выйдем из внутреннего цикла
                                break;
                            }
                        }
                    }
                }

                av_packet_unref(pkt);
            }

            av_packet_free(&pkt);
            av_frame_free(&frm);
            close_input();

            // перед реконнектом маленька€ пауза
            if (m_run.load()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    bool Stream::open_input() {
        close_input();

        // формат
        if (avformat_open_input(&m_fmt, m_url.c_str(), nullptr, nullptr) < 0) {
            return false;
        }
        avformat_find_stream_info(m_fmt, nullptr);

        // найдЄм видео поток
        m_vst_index = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

        // видеодекодер (нужен дл€ подсчЄта кадров decode)
        if (m_vst_index >= 0) {
            AVStream* vst = m_fmt->streams[m_vst_index];
            pick_input_fps(vst);

            const AVCodec* vcodec = avcodec_find_decoder(vst->codecpar->codec_id);
            if (vcodec) {
                m_vdec = avcodec_alloc_context3(vcodec);
                avcodec_parameters_to_context(m_vdec, vst->codecpar);

#if defined(_WIN32)
                // при желании здесь можно подключить D3D11VA/other HW Ч тогда подпишем как GPU
                m_decoder_label = "CPU";
#else
                m_decoder_label = "CPU";
#endif

                if (avcodec_open2(m_vdec, vcodec, nullptr) < 0) {
                    avcodec_free_context(&m_vdec);
                    m_vdec = nullptr;
                }
            }
            else {
                m_decoder_label = "CPU";
            }
        }
        else {
            // не нашли видео Ц хот€ бы дефолт FPS дл€ отображени€
            m_input_fps_hint = 25.0;
        }

        // PSI/PMT/SID/PID (если есть) Ц заберЄм из AVProgram
        probe_program_info();

        // старт окон fps и kbps
        {
            std::lock_guard<std::mutex> lk(m_mx);
            m_dec_sample_t0 = std::chrono::steady_clock::now();
            m_dec_sample_frames = 0;
            if (m_dec_fps_ema <= 0.0) m_dec_fps_ema = m_input_fps_hint;

            m_bw_t0 = std::chrono::steady_clock::now();
            m_bw_bits_total = 0;
            m_kbps = m_vkbps = m_akbps = 0;
            m_kbps_win_sum = m_kbps_win_cnt = 0;
            m_last_error.clear();
        }

        return true;
    }

    void Stream::close_input() {
        if (m_vdec) {
            avcodec_free_context(&m_vdec);
            m_vdec = nullptr;
        }
        if (m_fmt) {
            avformat_close_input(&m_fmt);
            m_fmt = nullptr;
        }
    }

    void Stream::pick_input_fps(AVStream* st) {
        double fps = 0.0;
        if (st) {
            if (st->avg_frame_rate.num && st->avg_frame_rate.den)
                fps = av_q2d(st->avg_frame_rate);
            if (fps <= 1e-6 && st->r_frame_rate.num && st->r_frame_rate.den)
                fps = av_q2d(st->r_frame_rate);
        }
        if (fps <= 1e-6) fps = 25.0; // дефолт, если источник не даЄт €вного FPS
        std::lock_guard<std::mutex> lk(m_mx);
        m_input_fps_hint = fps;
        if (m_dec_fps_ema <= 0.0) m_dec_fps_ema = fps; // быстрый прогрев
    }

    void Stream::on_video_frame_decoded() {
        using clock = std::chrono::steady_clock;

        std::lock_guard<std::mutex> lk(m_mx);
        m_dec_sample_frames++;

        auto now = clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_dec_sample_t0).count();
        if (ms >= 1000) {
            const double dt = ms / 1000.0;
            const double inst = (dt > 0.0) ? (m_dec_sample_frames / dt) : 0.0;

            // EWMA сглаживание (0.2Ц0.3 Ч Ђкомфортної)
            constexpr double alpha = 0.25;
            m_dec_fps_ema = (m_dec_fps_ema <= 0.0) ? inst : (m_dec_fps_ema + alpha * (inst - m_dec_fps_ema));

            m_dec_sample_frames = 0;
            m_dec_sample_t0 = now;
        }
    }

    void Stream::update_bitrate_window(int pkt_bits, bool is_video, bool is_audio) {
        using clock = std::chrono::steady_clock;
        auto now = clock::now();

        std::lock_guard<std::mutex> lk(m_mx);
        m_bw_bits_total += pkt_bits;

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_bw_t0).count();
        if (ms >= 1000) {
            const double dt = ms / 1000.0;
            int kbps = (int)std::llround((double)m_bw_bits_total / 1000.0 / dt); // kbits/s ~ kbps
            m_kbps = kbps;

            // упрощЄнное разделение V/A (последний тип пакета)
            if (is_video) m_vkbps = kbps;
            if (is_audio) m_akbps = kbps;

            // проста€ эвристика VBR/CBR: считаем среднее по окну и сравниваем
            m_kbps_win_sum += kbps; m_kbps_win_cnt++;
            if (m_kbps_win_cnt >= 6) { // окно ~6с
                double mean = (double)m_kbps_win_sum / (double)m_kbps_win_cnt;
                if (mean > 1.0 && std::abs(kbps - mean) / mean < 0.10) m_rate_mode = "CBR";
                else m_rate_mode = "VBR";
                m_kbps_win_sum = 0; m_kbps_win_cnt = 0;
            }

            m_bw_bits_total = 0;
            m_bw_t0 = now;
        }
    }

    void Stream::probe_program_info() {
        // ƒл€ MPEG-TS AVFormat формирует AVProgram'ы Ц заберЄм SID/PMT/PCR/ES PID.
        m_sid = -1; m_pmt_pid = -1; m_pcr_pid = -1; m_video_pid = -1;
        m_audio_pids.clear(); m_service_name.clear();

        if (!m_fmt) return;

        // если есть active program Ц возьмЄм еЄ (или первую)
        AVProgram* best = nullptr;
        if (m_fmt->nb_programs > 0) best = m_fmt->programs[0];
        if (!best) return;

        m_sid = best->id;                 // service id
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(59, 0, 100)
        m_pmt_pid = best->pmt_pid;
        m_pcr_pid = best->pcr_pid;
#endif

        for (unsigned i = 0; i < best->nb_stream_indexes; ++i) {
            int si = best->stream_index[i];
            if (si < 0 || si >= (int)m_fmt->nb_streams) continue;
            AVStream* st = m_fmt->streams[si];
            if (!st || !st->codecpar) continue;

            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                m_video_pid = st->id; // PID
            }
            else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                m_audio_pids.push_back(st->id);
            }
        }

        // service name, если доступно
        AVDictionaryEntry* e = nullptr;
        if (best->metadata && (e = av_dict_get(best->metadata, "service_name", nullptr, 0))) {
            m_service_name = e->value ? e->value : "";
        }
    }

} // namespace multiscreen
