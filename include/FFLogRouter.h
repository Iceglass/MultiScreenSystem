#pragma once
#include <unordered_map>
#include <functional>
#include <mutex>
#include <string>

extern "C" {
#include <libavutil/log.h>
}

class FFLogRouter {
public:
    using Handler = std::function<void(int level, const std::string& line)>;

    static FFLogRouter& instance();

    // Устанавливает глобальный callback FFmpeg (однократно, потокобезопасно)
    void install();

    // Привязать обработчик к конкретному источнику (например, AVFormatContext*)
    void registerSource(void* ctx, Handler h);
    void unregisterSource(void* ctx);

private:
    // Глобальный FFmpeg callback
    static void ffCallback(void* avcl, int level, const char* fmt, va_list vl);
    // Диспетчеризация в привязанный обработчик (уже не static)
    void dispatch(void* avcl, int level, const std::string& line);

    std::mutex mtx_;
    std::unordered_map<void*, Handler> handlers_;
    bool installed_ = false;
};
