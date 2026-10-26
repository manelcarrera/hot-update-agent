#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "nx/client.hpp"

namespace fs = std::filesystem;

namespace {
fs::path temp_dir(const char* name) {
    auto d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
}  // namespace

TEST(ModuleHost, LoadsAndSwapsWithoutRestart) {
    nx::ModuleHost host(temp_dir("nx_host_swap"));
    ASSERT_TRUE(host.load(NX_TEST_V1));
    EXPECT_EQ(host.current_version(), "1.0.0");

    auto held = host.current();  // simulates a call still running on v1
    ASSERT_TRUE(host.load(NX_TEST_V2));
    EXPECT_EQ(host.current_version(), "2.0.0");
    EXPECT_EQ(held->version(), "1.0.0");  // the old module stays valid while in use
    held->run();
}

TEST(ModuleHost, RejectsNonLibraryAndKeepsCurrent) {
    auto dir = temp_dir("nx_host_bad");
    nx::ModuleHost host(dir / "runtime");
    ASSERT_TRUE(host.load(NX_TEST_V1));
    const auto bogus = dir / "bogus.bin";
    std::ofstream(bogus) << "not a shared library";
    std::string error;
    EXPECT_FALSE(host.load(bogus, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(host.current_version(), "1.0.0");
}

TEST(ModuleHost, SameFileTwiceGivesTwoIndependentLoads) {
    nx::ModuleHost host(temp_dir("nx_host_twice"));
    ASSERT_TRUE(host.load(NX_TEST_V1));
    auto first = host.current();
    ASSERT_TRUE(host.load(NX_TEST_V1));
    EXPECT_NE(first.get(), host.current().get());
}
