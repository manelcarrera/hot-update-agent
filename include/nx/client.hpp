#pragma once
// Client side: hosting the functional part and keeping it up to date.

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <stop_token>
#include <thread>

#include "nx/common.hpp"

namespace nx {

namespace net { class Socket; }

// One loaded copy of the functional part. Unloaded when the last user drops it,
// so a call that is running while an update arrives finishes on the old code.
class Module {
public:
    ~Module();
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    const std::string& version() const { return version_; }
    void run() const;  // the functional part: prints its version

private:
    friend class ModuleHost;
    Module() = default;
    void* handle_ = nullptr;
    void (*run_)() = nullptr;
    std::string version_;
    std::filesystem::path loaded_copy_;
};

// Loads a module and swaps it in without restarting the process.
//
// Each version is loaded from a private copy with a unique name: on Windows a DLL
// in use cannot be overwritten, and on every platform the loader would otherwise
// hand back the already-loaded image for the same path.
class ModuleHost {
public:
    explicit ModuleHost(std::filesystem::path runtime_dir);

    // Loads, validates (symbols, ABI, version) and makes it current. On any
    // failure the current module keeps running and false is returned.
    bool load(const std::filesystem::path& library, std::string* error = nullptr);

    std::shared_ptr<const Module> current() const;
    std::string current_version() const;  // "" if nothing loaded

private:
    std::filesystem::path runtime_dir_;
    std::atomic<std::uint64_t> counter_{0};
    mutable std::mutex mutex_;  // guards current_ (std::atomic<shared_ptr> is not portable enough yet)
    std::shared_ptr<const Module> current_;
};

// Keeps a connection to the update server, receives pushed notifications,
// downloads, verifies and hands new modules to the host. Reconnects with backoff.
class UpdateAgent {
public:
    struct Options {
        std::string host = "127.0.0.1";
        std::uint16_t port = 47800;
        std::filesystem::path cache_dir = "nx-cache";
    };
    UpdateAgent(Options options, ModuleHost& host);
    ~UpdateAgent();  // stops and joins

    void start();
    void stop();

private:
    void run(std::stop_token stop);
    void session(net::Socket& socket, std::stop_token stop);

    Options options_;
    ModuleHost& host_;
    std::jthread thread_;
};

}  // namespace nx
