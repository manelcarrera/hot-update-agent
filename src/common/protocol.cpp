#include "nx/protocol.hpp"

#include <cstring>

namespace nx::proto {
namespace {

class Writer {
public:
    void u8(std::uint8_t v) { out_.push_back(v); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) out_.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
    void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) out_.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
    void str(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())); out_.insert(out_.end(), s.begin(), s.end()); }
    void digest(const Digest& d) { out_.insert(out_.end(), d.begin(), d.end()); }
    void blob(const Bytes& b) { u32(static_cast<std::uint32_t>(b.size())); out_.insert(out_.end(), b.begin(), b.end()); }
    Bytes take() { return std::move(out_); }
    std::size_t size() const { return out_.size(); }
    std::uint8_t* data() { return out_.data(); }

private:
    Bytes out_;
};

// Bounds-checked reader: every read fails cleanly instead of running past the end.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> in) : in_(in) {}
    bool u8(std::uint8_t& v) { if (!need(1)) return false; v = in_[pos_++]; return true; }
    bool u32(std::uint32_t& v) {
        if (!need(4)) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t{in_[pos_++]} << (8 * i);
        return true;
    }
    bool u64(std::uint64_t& v) {
        if (!need(8)) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= std::uint64_t{in_[pos_++]} << (8 * i);
        return true;
    }
    bool str(std::string& s) {
        std::uint32_t n = 0;
        if (!u32(n) || !need(n)) return false;
        s.assign(reinterpret_cast<const char*>(in_.data() + pos_), n);
        pos_ += n;
        return true;
    }
    bool digest(Digest& d) {
        if (!need(d.size())) return false;
        std::memcpy(d.data(), in_.data() + pos_, d.size());
        pos_ += d.size();
        return true;
    }
    bool blob(Bytes& b) {
        std::uint32_t n = 0;
        if (!u32(n) || !need(n)) return false;
        b.assign(in_.begin() + static_cast<std::ptrdiff_t>(pos_), in_.begin() + static_cast<std::ptrdiff_t>(pos_ + n));
        pos_ += n;
        return true;
    }
    bool done() const { return pos_ == in_.size(); }

private:
    bool need(std::size_t n) const { return n <= in_.size() - pos_; }
    std::span<const std::uint8_t> in_;
    std::size_t pos_ = 0;
};

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

}  // namespace

Bytes encode(const Message& m) {
    Writer w;
    w.u32(0);  // length, patched below
    std::visit(overloaded{
        [&](const Hello& x) { w.u8(std::uint8_t(Type::Hello)); w.str(x.version); w.str(x.platform); },
        [&](const UpdateAvailable& x) { w.u8(std::uint8_t(Type::UpdateAvailable)); w.str(x.version); w.u64(x.size); w.digest(x.sha); },
        [&](const Get& x) { w.u8(std::uint8_t(Type::Get)); w.str(x.version); },
        [&](const Module& x) { w.u8(std::uint8_t(Type::Module)); w.str(x.version); w.digest(x.sha); w.blob(x.data); },
        [&](const Publish& x) { w.u8(std::uint8_t(Type::Publish)); w.str(x.version); w.str(x.platform); w.blob(x.data); },
        [&](const Ack& x) { w.u8(std::uint8_t(Type::Ack)); w.str(x.text); },
        [&](const Error& x) { w.u8(std::uint8_t(Type::Error)); w.str(x.text); },
    }, m);
    const auto len = static_cast<std::uint32_t>(w.size() - 4);
    for (int i = 0; i < 4; ++i) w.data()[i] = static_cast<std::uint8_t>(len >> (8 * i));
    return w.take();
}

std::optional<Message> decode(std::span<const std::uint8_t> body) {
    Reader r(body);
    std::uint8_t t = 0;
    if (!r.u8(t)) return std::nullopt;
    std::optional<Message> out;
    switch (static_cast<Type>(t)) {
        case Type::Hello: { Hello x; if (r.str(x.version) && r.str(x.platform)) out = std::move(x); break; }
        case Type::UpdateAvailable: { UpdateAvailable x; if (r.str(x.version) && r.u64(x.size) && r.digest(x.sha)) out = std::move(x); break; }
        case Type::Get: { Get x; if (r.str(x.version)) out = std::move(x); break; }
        case Type::Module: { Module x; if (r.str(x.version) && r.digest(x.sha) && r.blob(x.data)) out = std::move(x); break; }
        case Type::Publish: { Publish x; if (r.str(x.version) && r.str(x.platform) && r.blob(x.data)) out = std::move(x); break; }
        case Type::Ack: { Ack x; if (r.str(x.text)) out = std::move(x); break; }
        case Type::Error: { Error x; if (r.str(x.text)) out = std::move(x); break; }
        default: return std::nullopt;
    }
    if (!out || !r.done()) return std::nullopt;  // trailing bytes = malformed
    return out;
}

}  // namespace nx::proto
