#include "nx/server.hpp"

#include <algorithm>

#include "nx/net.hpp"

namespace nx {

// A connected peer. `send_mutex` serialises writes: the serving thread and the
// thread that publishes (notify_all) may write to the same socket.
struct UpdateServer::Client {
    net::Socket socket;
    std::mutex send_mutex;
    std::string platform;   // set by HELLO; empty for publishers
    bool subscribed = false;

    bool send(const proto::Message& m) {
        const std::lock_guard lock(send_mutex);
        return net::send_message(socket, m);
    }
};

UpdateServer::UpdateServer(Options options) : options_(std::move(options)) {}

UpdateServer::~UpdateServer() { stop(); }

bool UpdateServer::start() {
    auto s = net::Socket::listen(options_.bind, options_.port);
    if (!s) return false;
    port_ = s->local_port();
    listener_ = std::make_unique<net::Socket>(std::move(*s));
    load_store();
    acceptor_ = std::jthread([this](std::stop_token st) { accept_loop(st); });
    log("server", "listening on " + options_.bind + ":" + std::to_string(port_));
    return true;
}

void UpdateServer::stop() {
    if (!acceptor_.joinable()) return;
    acceptor_.request_stop();
    acceptor_.join();
    std::vector<std::jthread> workers;
    {
        const std::lock_guard lock(mutex_);
        for (auto& c : clients_) c->socket.shutdown();  // wakes up their recv
        workers = std::move(workers_);
    }
    for (auto& w : workers) { w.request_stop(); w.join(); }
    const std::lock_guard lock(mutex_);
    clients_.clear();
}

void UpdateServer::accept_loop(std::stop_token stop) {
    while (!stop.stop_requested()) {
        if (!listener_->wait_readable(100)) continue;
        auto s = listener_->accept();
        if (!s) continue;
        auto client = std::make_shared<Client>();
        client->socket = std::move(*s);
        const std::lock_guard lock(mutex_);
        clients_.push_back(client);
        workers_.emplace_back([this, client](std::stop_token st) { serve(client, st); });
    }
}

void UpdateServer::serve(std::shared_ptr<Client> client, std::stop_token stop) {
    while (!stop.stop_requested()) {
        if (!client->socket.wait_readable(100)) continue;
        auto msg = net::recv_message(client->socket);
        if (!msg) break;  // EOF, error or malformed frame: drop the peer

        if (auto* hello = std::get_if<proto::Hello>(&*msg)) {
            {
                const std::lock_guard lock(mutex_);
                client->platform = hello->platform;
                client->subscribed = true;
            }
            log("server", "client " + hello->platform + " running " + (hello->version.empty() ? "?" : hello->version));
            const auto have = Version::parse(hello->version);
            if (auto a = latest(hello->platform); a && (!have || a->version > *have))
                client->send(proto::UpdateAvailable{a->version.str(), a->data->size(), a->sha});

        } else if (auto* get = std::get_if<proto::Get>(&*msg)) {
            auto a = latest(client->platform);
            if (!a || a->version.str() != get->version) {
                client->send(proto::Error{"version " + get->version + " not available"});
                continue;
            }
            client->send(proto::Module{a->version.str(), a->sha, *a->data});

        } else if (auto* pub = std::get_if<proto::Publish>(&*msg)) {
            std::string error;
            const auto v = Version::parse(pub->version);
            if (!v) client->send(proto::Error{"invalid version '" + pub->version + "'"});
            else if (publish(pub->platform, *v, std::move(pub->data), error)) client->send(proto::Ack{"published " + v->str()});
            else client->send(proto::Error{error});

        } else {
            client->send(proto::Error{"unexpected message"});
        }
    }
    const std::lock_guard lock(mutex_);
    std::erase(clients_, client);
}

bool UpdateServer::publish(const std::string& platform, const Version& v, Bytes data, std::string& error) {
    if (data.empty()) { error = "empty module"; return false; }
    Artifact a{v, nullptr, sha256(data)};
    {
        const std::lock_guard lock(mutex_);
        if (auto it = artifacts_.find(platform); it != artifacts_.end() && !(it->second.version < v)) {
            error = "version " + v.str() + " is not newer than " + it->second.version.str();
            return false;
        }
    }
    // Persist before announcing, so a restart serves the same version.
    const auto file = options_.store / platform / ("functional-" + v.str() + ".bin");
    if (!write_file_atomic(file, data)) { error = "cannot store " + file.string(); return false; }
    a.data = std::make_shared<const Bytes>(std::move(data));
    {
        const std::lock_guard lock(mutex_);
        artifacts_[platform] = a;
    }
    log("server", "published " + v.str() + " for " + platform + " (" + std::to_string(a.data->size()) +
                      " bytes, sha256 " + to_hex(a.sha).substr(0, 12) + "...)");
    notify_all(platform, a);
    return true;
}

void UpdateServer::notify_all(const std::string& platform, const Artifact& a) {
    std::vector<std::shared_ptr<Client>> targets;
    {
        const std::lock_guard lock(mutex_);
        for (auto& c : clients_)
            if (c->subscribed && c->platform == platform) targets.push_back(c);
    }
    for (auto& c : targets) c->send(proto::UpdateAvailable{a.version.str(), a.data->size(), a.sha});  // outside the lock
}

std::optional<UpdateServer::Artifact> UpdateServer::latest(const std::string& platform) const {
    const std::lock_guard lock(mutex_);
    if (auto it = artifacts_.find(platform); it != artifacts_.end()) return it->second;
    return std::nullopt;
}

void UpdateServer::load_store() {
    std::error_code ec;
    for (const auto& dir : std::filesystem::directory_iterator(options_.store, ec)) {
        if (!dir.is_directory()) continue;
        for (const auto& f : std::filesystem::directory_iterator(dir.path(), ec)) {
            const auto name = f.path().stem().string();  // functional-1.2.3
            if (!name.starts_with("functional-")) continue;
            const auto v = Version::parse(name.substr(11));
            auto data = read_file(f.path());
            if (!v || !data || data->empty()) continue;
            const auto platform = dir.path().filename().string();
            auto& slot = artifacts_[platform];
            if (!slot.data || slot.version < *v)
                slot = Artifact{*v, std::make_shared<const Bytes>(std::move(*data)), {}};
        }
    }
    for (auto& [platform, a] : artifacts_) {
        a.sha = sha256(*a.data);
        log("server", "stored " + a.version.str() + " for " + platform);
    }
}

}  // namespace nx
