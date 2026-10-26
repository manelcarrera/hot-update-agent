#include "nx/publisher.hpp"

#include "nx/net.hpp"

namespace nx {

bool publish_module(const std::string& host, std::uint16_t port, const std::string& version,
                    const std::string& platform, const std::filesystem::path& library, std::string& reply) {
    auto data = read_file(library);
    if (!data) { reply = "cannot read " + library.string(); return false; }
    auto socket = net::Socket::connect(host, port);
    if (!socket) { reply = "cannot connect to " + host + ":" + std::to_string(port); return false; }
    if (!net::send_message(*socket, proto::Publish{version, platform, std::move(*data)})) { reply = "send failed"; return false; }
    if (!socket->wait_readable(10000)) { reply = "no answer from the server"; return false; }
    auto msg = net::recv_message(*socket);
    if (auto* ack = msg ? std::get_if<proto::Ack>(&*msg) : nullptr) { reply = ack->text; return true; }
    if (auto* err = msg ? std::get_if<proto::Error>(&*msg) : nullptr) { reply = err->text; return false; }
    reply = "unexpected answer";
    return false;
}

}  // namespace nx
