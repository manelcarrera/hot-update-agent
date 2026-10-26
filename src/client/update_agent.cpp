#include "nx/client.hpp"

#include <chrono>

#include "nx/net.hpp"

namespace nx {

UpdateAgent::UpdateAgent(Options options, ModuleHost& host) : options_(std::move(options)), host_(host) {}

UpdateAgent::~UpdateAgent() { stop(); }

void UpdateAgent::start() {
    thread_ = std::jthread([this](std::stop_token st) { run(st); });
}

void UpdateAgent::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void UpdateAgent::run(std::stop_token stop) {
    using namespace std::chrono_literals;
    auto backoff = 250ms;
    while (!stop.stop_requested()) {
        if (auto socket = net::Socket::connect(options_.host, options_.port)) {
            log("agent", "connected to " + options_.host + ":" + std::to_string(options_.port));
            backoff = 250ms;
            session(*socket, stop);
            log("agent", "disconnected");
        }
        // Reconnect with exponential backoff (cap 5 s), waking up early on stop.
        for (auto waited = 0ms; waited < backoff && !stop.stop_requested(); waited += 50ms)
            std::this_thread::sleep_for(50ms);
        backoff = std::min(backoff * 2, std::chrono::milliseconds(5000));
    }
}

void UpdateAgent::session(net::Socket& socket, std::stop_token stop) {
    if (!net::send_message(socket, proto::Hello{host_.current_version(), std::string(platform_id())})) return;

    while (!stop.stop_requested()) {
        if (!socket.wait_readable(100)) continue;  // lets stop() take effect within 100 ms
        auto msg = net::recv_message(socket);
        if (!msg) return;

        if (auto* avail = std::get_if<proto::UpdateAvailable>(&*msg)) {
            const auto offered = Version::parse(avail->version);
            const auto mine = Version::parse(host_.current_version());
            if (!offered || (mine && *offered <= *mine)) continue;  // nothing newer
            log("agent", "update available: " + avail->version);
            if (!net::send_message(socket, proto::Get{avail->version})) return;

        } else if (auto* mod = std::get_if<proto::Module>(&*msg)) {
            // Integrity first: what we received must be what was announced.
            if (sha256(mod->data) != mod->sha) {
                log("agent", "checksum mismatch for " + mod->version + ", ignored");
                continue;
            }
            const auto file = options_.cache_dir / ("functional-" + mod->version + std::string(shared_lib_extension()));
            if (!write_file_atomic(file, mod->data)) {
                log("agent", "cannot write " + file.string());
                continue;
            }
            std::string error;
            if (!host_.load(file, &error)) log("agent", "keeping " + host_.current_version() + ": " + error);

        } else if (auto* err = std::get_if<proto::Error>(&*msg)) {
            log("agent", "server error: " + err->text);
        }
    }
}

}  // namespace nx
