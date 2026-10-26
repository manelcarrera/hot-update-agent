#include <gtest/gtest.h>

#include "nx/common.hpp"

using nx::Version;

TEST(Version, ParseAndCompare) {
    auto a = Version::parse("1.9.0");
    auto b = Version::parse("1.10.0");  // numeric, not lexicographic
    ASSERT_TRUE(a && b);
    EXPECT_LT(*a, *b);
    EXPECT_EQ(b->str(), "1.10.0");
}

TEST(Version, RejectsGarbage) {
    for (const char* s : {"", "1", "1.2", "1.2.3.4", "a.b.c", "1.2.x", " 1.2.3", "1..3", "-1.2.3"})
        EXPECT_FALSE(Version::parse(s)) << s;
}
