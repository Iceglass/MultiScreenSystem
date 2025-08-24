// src/main.cpp
#include "Application.h"
#include "Settings.h"

#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <iostream>

static std::atomic<bool> g_run{ true };

static void on_signal(int) {
    g_run.store(false);
}

int main(int /*argc*/, char** /*argv*/) {
    // Грейсфул-стоп по Ctrl+C / SIGTERM
    std::signal(SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal(SIGTERM, on_signal);
#endif

    // Загружаем дополнительные пороги/алерты (как и было раньше)
    // Если файла нет — Settings::load вернёт false, это некритично.
    multiscreen::Settings::instance().load("config/settings.json");

    // Единая инициализация приложения:
    // Application читает config/config.json (web.port, web.enable, streams.enable)
    // и config/streams.json (список стримов), запускает WebServer и потоки стримов.
    multiscreen::Application app;
    if (!app.initialize("config")) {
        std::cerr << "[ERROR] Application.initialize failed\n";
        return 1;
    }

    std::cout << "[INFO] Running. Press Ctrl+C to stop.\n";
    while (g_run.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "[INFO] Shutting down...\n";
    app.shutdown();
    std::cout << "[INFO] Bye!\n";
    return 0;
}
