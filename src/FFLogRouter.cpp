#include "FFLogRouter.h"
#include <cstdarg>
#include <cstdio>

FFLogRouter& FFLogRouter::instance() {
    static FFLogRouter inst;
    return inst;
}

void FFLogRouter::install() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (installed_) return;
    av_log_set_callback(&FFLogRouter::ffCallback);
    // �������������� � ���� ���������� (���� �������� continuity errors)
    av_log_set_level(AV_LOG_WARNING);
    installed_ = true;
}

void FFLogRouter::registerSource(void* ctx, Handler h) {
    if (!ctx) return;
    std::lock_guard<std::mutex> lk(mtx_);
    handlers_[ctx] = std::move(h);
}

void FFLogRouter::unregisterSource(void* ctx) {
    if (!ctx) return;
    std::lock_guard<std::mutex> lk(mtx_);
    handlers_.erase(ctx);
}

void FFLogRouter::ffCallback(void* avcl, int level, const char* fmt, va_list vl) {
    char buf[1024];
#if defined(_MSC_VER)
    vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, vl);
#else
    vsnprintf(buf, sizeof(buf), fmt, vl);
#endif
    // ������� ��������� � ������� ������ ��� std::string
    FFLogRouter::instance().dispatch(avcl, level, std::string(buf));
}

void FFLogRouter::dispatch(void* avcl, int level, const std::string& line) {
    Handler h;

    // ���� ������ �������� ��� ���������
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = handlers_.find(avcl);
        if (it != handlers_.end()) {
            h = it->second;
        }
    }

    // ���� ����� � �������� ��� ��� ��������
    if (h) {
        h(level, line);
    }
    // ���� �� �����: ����� ���������� (����� ��������� ����� ��������� �� ���������� �������� FFmpeg)
}
