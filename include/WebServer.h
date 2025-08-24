#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace multiscreen {

    class StreamManager;

    class WebServer {
    public:
        explicit WebServer(StreamManager& mgr);
        ~WebServer();

        bool start(int port);
        void stop();

    private:
        // ВАЖНО: реализованы как методы класса (чтобы не было unresolved externals)
        static nlohmann::json parse_json(const std::string& body);
        static void persist_append_stream(const std::string& name, const std::string& url);
        static void persist_remove_stream(const std::string& name);
        static std::string load_index_html_from_disk();

    private:
        StreamManager& m_mgr;
        int                                 m_port{ 0 };
        std::unique_ptr<httplib::Server>    m_svr;
        std::thread                         m_thread;
        std::atomic<bool>                   m_running{ false };
    };

} // namespace multiscreen
