#pragma once
// Minimal blocking TCP sockets over POSIX and Winsock, plus framed messages.
// Blocking calls are bounded with wait_readable() so threads can stop promptly.

#include <cstdint>
#include <optional>
#include <string>

#include "nx/protocol.hpp"

namespace nx::net {

class Socket {
public:
    Socket() = default;
    ~Socket();
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    static std::optional<Socket> connect(const std::string& host, std::uint16_t port);
    // port 0 = let the OS choose (used by the tests); see local_port().
    static std::optional<Socket> listen(const std::string& bind_host, std::uint16_t port);
    std::optional<Socket> accept();
    std::uint16_t local_port() const;

    // true if data (or an incoming connection, or EOF) is ready within timeout_ms.
    bool wait_readable(int timeout_ms) const;
    bool send_all(std::span<const std::uint8_t> data);
    bool recv_exact(std::uint8_t* out, std::size_t n);
    void shutdown();  // wakes up a thread blocked in recv on this socket
    bool valid() const;

private:
    explicit Socket(std::intptr_t fd) : fd_(fd) {}
    void close();
    std::intptr_t fd_ = -1;
};

bool send_message(Socket& s, const proto::Message& m);
// nullopt on EOF, error or malformed frame (the connection must then be dropped).
std::optional<proto::Message> recv_message(Socket& s);

}  // namespace nx::net
