#pragma once
#include <memory>
#include <string>
#include "StreamManager.h"
#include "WebServer.h"

namespace multiscreen {

class Application {
public:
    Application();
    ~Application();

    bool initialize(const std::string& configDir = "config");
    void run();
    void shutdown();

private:
    std::unique_ptr<StreamManager> m_mgr;
    std::unique_ptr<WebServer> m_web;
};

} // namespace multiscreen
