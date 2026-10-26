#include <gtest/gtest.h>

#include <random>

#include "nx/protocol.hpp"

using namespace nx;
using namespace nx::proto;

namespace {
std::optional<Message> roundtrip(const Message& m) {
    const Bytes frame = encode(m);
    std::uint32_t len = 0;
    for (int i = 0; i < 4; ++i) len |= std::uint32_t{frame[static_cast<std::size_t>(i)]} << (8 * i);
    EXPECT_EQ(len, frame.size() - 4);
    return decode(std::span(frame).subspan(4));
}
}  // namespace

TEST(Protocol, HelloRoundtrip) {
    auto m = roundtrip(Hello{"1.2.3", "linux-x86_64"});
    ASSERT_TRUE(m);
    auto& h = std::get<Hello>(*m);
    EXPECT_EQ(h.version, "1.2.3");
    EXPECT_EQ(h.platform, "linux-x86_64");
}

TEST(Protocol, ModuleRoundtripKeepsBytesAndDigest) {
    Bytes data(100'000);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i * 31);
    auto m = roundtrip(Module{"2.0.0", sha256(data), data});
    ASSERT_TRUE(m);
    auto& mod = std::get<Module>(*m);
    EXPECT_EQ(mod.data, data);
    EXPECT_EQ(mod.sha, sha256(data));
}

TEST(Protocol, RejectsTruncatedAndTrailing) {
    const Bytes frame = encode(UpdateAvailable{"2.0.0", 42, {}});
    auto body = std::span(frame).subspan(4);
    for (std::size_t n = 0; n < body.size(); ++n) EXPECT_FALSE(decode(body.first(n))) << "length " << n;
    Bytes longer(body.begin(), body.end());
    longer.push_back(0);
    EXPECT_FALSE(decode(longer));
}

TEST(Protocol, RejectsUnknownType) {
    const Bytes body = {99};
    EXPECT_FALSE(decode(body));
}

TEST(Protocol, LengthFieldsCannotOverrun) {
    // A string that claims 4 GiB must fail cleanly, not read out of bounds.
    const Bytes body = {std::uint8_t(Type::Get), 0xff, 0xff, 0xff, 0xff, 'x'};
    EXPECT_FALSE(decode(body));
}

TEST(Protocol, FuzzNeverCrashes) {
    std::mt19937 rng(7);
    for (int i = 0; i < 20'000; ++i) {
        Bytes body(rng() % 64);
        for (auto& b : body) b = static_cast<std::uint8_t>(rng());
        if (!body.empty() && rng() % 2) body[0] = static_cast<std::uint8_t>(1 + rng() % 7);
        (void)decode(body);
    }
}
