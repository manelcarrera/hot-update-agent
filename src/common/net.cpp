#include "nx/net.hpp"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using native_socket = SOCKET;
#  define NX_INVALID INVALID_SOCKET
#  define NX_CLOSE closesocket
#  define NX_POLL WSAPoll
#  define NX_SHUT_BOTH SD_BOTH
#else
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
using native_socket = int;
#  define NX_INVALID (-1)
#  define NX_CLOSE ::close
#  define NX_POLL ::poll
#  define NX_SHUT_BOTH SHUT_RDWR
#endif

#include <cstring>

namespace nx::net {
namespace {

#if defined(_WIN32)
struct WinsockInit {
    WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WinsockInit() { WSACleanup(); }
};
const WinsockInit winsock_init;
#endif

native_socket raw(std::intptr_t fd) { return static_cast<native_socket>(fd); }

}  // namespace

Socket::~Socket() { close(); }
Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) { close(); fd_ = other.fd_; other.fd_ = -1; }
    return *this;
}
bool Socket::valid() const { return fd_ != -1; }

void Socket::close() {
    if (valid()) { NX_CLOSE(raw(fd_)); fd_ = -1; }
}

void Socket::shutdown() {
    if (valid()) ::shutdown(raw(fd_), NX_SHUT_BOTH);
}

std::optional<Socket> Socket::connect(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return std::nullopt;
    std::optional<Socket> out;
    for (auto* ai = res; ai && !out; ai = ai->ai_next) {
        native_socket fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == NX_INVALID) continue;
        if (::connect(fd, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
            out = Socket(static_cast<std::intptr_t>(fd));
        } else {
            NX_CLOSE(fd);
        }
    }
    freeaddrinfo(res);
    return out;
}

std::optional<Socket> Socket::listen(const std::string& bind_host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* res = nullptr;
    if (getaddrinfo(bind_host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return std::nullopt;
    native_socket fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    std::optional<Socket> out;
    if (fd != NX_INVALID) {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
        if (::bind(fd, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0 && ::listen(fd, 64) == 0)
            out = Socket(static_cast<std::intptr_t>(fd));
        else
            NX_CLOSE(fd);
    }
    freeaddrinfo(res);
    return out;
}

std::optional<Socket> Socket::accept() {
    native_socket fd = ::accept(raw(fd_), nullptr, nullptr);
    if (fd == NX_INVALID) return std::nullopt;
    return Socket(static_cast<std::intptr_t>(fd));
}

std::uint16_t Socket::local_port() const {
    sockaddr_in addr{};
    socklen_t len = sizeof addr;
    if (getsockname(raw(fd_), reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

bool Socket::wait_readable(int timeout_ms) const {
    pollfd p{};
    p.fd = raw(fd_);
    p.events = POLLIN;
    return NX_POLL(&p, 1, timeout_ms) > 0;
}

bool Socket::send_all(std::span<const std::uint8_t> data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto chunk = static_cast<int>(std::min<std::size_t>(data.size() - sent, 1 << 20));
#if defined(_WIN32)
        const int n = ::send(raw(fd_), reinterpret_cast<const char*>(data.data() + sent), chunk, 0);
#else
        const auto n = ::send(raw(fd_), data.data() + sent, static_cast<std::size_t>(chunk), MSG_NOSIGNAL);
#endif
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool Socket::recv_exact(std::uint8_t* out, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const auto chunk = static_cast<int>(std::min<std::size_t>(n - got, 1 << 20));
#if defined(_WIN32)
        const int r = ::recv(raw(fd_), reinterpret_cast<char*>(out + got), chunk, 0);
#else
        const auto r = ::recv(raw(fd_), out + got, static_cast<std::size_t>(chunk), 0);
#endif
        if (r <= 0) return false;
        got += static_cast<std::size_t>(r);
    }
    return true;
}

bool send_message(Socket& s, const proto::Message& m) {
    const Bytes frame = proto::encode(m);
    return s.send_all(frame);
}

std::optional<proto::Message> recv_message(Socket& s) {
    std::uint8_t header[4];
    if (!s.recv_exact(header, 4)) return std::nullopt;
    std::uint32_t len = 0;
    for (int i = 0; i < 4; ++i) len |= std::uint32_t{header[i]} << (8 * i);
    if (len == 0 || len > proto::kMaxFrame) return std::nullopt;
    Bytes body(len);
    if (!s.recv_exact(body.data(), len)) return std::nullopt;
    return proto::decode(body);
}

}  // namespace nx::net
