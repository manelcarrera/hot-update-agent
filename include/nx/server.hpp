#pragma once
// Update server: stores the latest functional part per platform and pushes it.

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "nx/common.hpp"

namespace nx {

namespace net { class Socket; }

class UpdateServer {
public:
    struct Options {
        std::string bind = "127.0.0.1";
        std::uint16_t port = 47800;           // 0 = any free port (tests)
        std::filesystem::path store = "nx-store";  // published modules survive restarts
    };
    explicit UpdateServer(Options options);
    ~UpdateServer();  // stops and joins every thread

    bool start();     // false if the port cannot be bound
    void stop();
    std::uint16_t port() const { return port_; }

private:
    struct Artifact {
        Version version;
        std::shared_ptr<const Bytes> data;  // shared: sending to many clients copies nothing
        Digest sha{};
    };
    struct Client;

    void accept_loop(std::stop_token stop);
    void serve(std::shared_ptr<Client> client, std::stop_token stop);
    bool publish(const std::string& platform, const Version& v, Bytes data, std::string& error);
    void notify_all(const std::string& platform, const Artifact& a);
    std::optional<Artifact> latest(const std::string& platform) const;
    void load_store();

    Options options_;
    std::uint16_t port_ = 0;
    std::unique_ptr<net::Socket> listener_;
    std::jthread acceptor_;

    mutable std::mutex mutex_;  // guards artifacts_, clients_, workers_
    std::map<std::string, Artifact> artifacts_;  // platform -> latest
    std::vector<std::shared_ptr<Client>> clients_;
    std::vector<std::jthread> workers_;
};

}  // namespace nx
