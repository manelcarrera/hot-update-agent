#include <gtest/gtest.h>

#include <string>

#include "nx/common.hpp"

namespace {
std::string hex_of(const std::string& s) {
    return nx::to_hex(nx::sha256({reinterpret_cast<const std::uint8_t*>(s.data()), s.size()}));
}
}  // namespace

// NIST FIPS 180-4 example vectors.
TEST(Sha256, Empty) {
    EXPECT_EQ(hex_of(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}
TEST(Sha256, Abc) {
    EXPECT_EQ(hex_of("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
TEST(Sha256, TwoBlocks) {
    EXPECT_EQ(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}
TEST(Sha256, MillionA) {
    EXPECT_EQ(hex_of(std::string(1'000'000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
