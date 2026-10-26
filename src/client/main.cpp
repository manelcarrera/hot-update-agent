// nx-client: runs the functional part and keeps it up to date without restarting.
//
//   nx-client --module <initial library> [--host 127.0.0.1] [--port 47800] [--cache nx-cache]
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "nx/client.hpp"

namespace {
std::atomic<bool> g_quit{false};  // lock-free: safe to set from a signal handler
extern "C" void on_signal(int) { g_quit.store(true); }
}  // namespace

int main(int argc, char** argv) {
    nx::UpdateAgent::Options opt;
    std::string module;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--module")) module = argv[i + 1];
        else if (!std::strcmp(argv[i], "--host")) opt.host = argv[i + 1];
        else if (!std::strcmp(argv[i], "--port")) opt.port = static_cast<std::uint16_t>(std::stoi(argv[i + 1]));
        else if (!std::strcmp(argv[i], "--cache")) opt.cache_dir = argv[i + 1];
    }
    if (module.empty()) {
        std::fprintf(stderr, "usage: nx-client --module <library> [--host H] [--port P] [--cache DIR]\n");
        return 2;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    nx::ModuleHost host(opt.cache_dir / "runtime");
    if (!host.load(module)) return 1;  // prints the initial version

    nx::UpdateAgent agent(opt, host);
    agent.start();
    while (!g_quit.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    agent.stop();
    nx::log("client", "bye, running " + host.current_version());
    return 0;
}
