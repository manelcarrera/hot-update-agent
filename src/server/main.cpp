// nx-server: update service.   nx-server [--bind 127.0.0.1] [--port 47800] [--store nx-store]
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "nx/server.hpp"

namespace {
std::atomic<bool> g_quit{false};
extern "C" void on_signal(int) { g_quit.store(true); }
}  // namespace

int main(int argc, char** argv) {
    nx::UpdateServer::Options opt;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--bind")) opt.bind = argv[i + 1];
        else if (!std::strcmp(argv[i], "--port")) opt.port = static_cast<std::uint16_t>(std::stoi(argv[i + 1]));
        else if (!std::strcmp(argv[i], "--store")) opt.store = argv[i + 1];
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    nx::UpdateServer server(opt);
    if (!server.start()) {
        std::fprintf(stderr, "cannot listen on %s:%u\n", opt.bind.c_str(), unsigned{opt.port});
        return 1;
    }
    while (!g_quit.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.stop();  // graceful: closes connections and joins every thread
    nx::log("server", "stopped");
    return 0;
}
