// Whole flow in one process: server, client agent and publisher on a free port.
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "nx/client.hpp"
#include "nx/publisher.hpp"
#include "nx/server.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
fs::path temp_dir(const char* name) {
    auto d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

template <class Pred> bool wait_for(Pred pred, std::chrono::milliseconds timeout = 5000ms) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return pred();
}
}  // namespace

TEST(EndToEnd, PublishedVersionReachesRunningClient) {
    const auto dir = temp_dir("nx_e2e");
    nx::UpdateServer server({"127.0.0.1", 0, dir / "store"});
    ASSERT_TRUE(server.start());

    nx::ModuleHost host(dir / "client" / "runtime");
    ASSERT_TRUE(host.load(NX_TEST_V1));
    nx::UpdateAgent agent({"127.0.0.1", server.port(), dir / "client"}, host);
    agent.start();
    std::this_thread::sleep_for(200ms);  // connected and subscribed, nothing to update yet
    EXPECT_EQ(host.current_version(), "1.0.0");

    std::string reply;
    ASSERT_TRUE(nx::publish_module("127.0.0.1", server.port(), "2.0.0", std::string(nx::platform_id()), NX_TEST_V2, reply)) << reply;

    // Pushed, downloaded, verified and swapped in without restarting.
    EXPECT_TRUE(wait_for([&] { return host.current_version() == "2.0.0"; }));
}

TEST(EndToEnd, LateClientGetsLatestOnConnect) {
    const auto dir = temp_dir("nx_e2e_late");
    nx::UpdateServer server({"127.0.0.1", 0, dir / "store"});
    ASSERT_TRUE(server.start());
    std::string reply;
    ASSERT_TRUE(nx::publish_module("127.0.0.1", server.port(), "2.0.0", std::string(nx::platform_id()), NX_TEST_V2, reply));

    nx::ModuleHost host(dir / "client" / "runtime");
    ASSERT_TRUE(host.load(NX_TEST_V1));
    nx::UpdateAgent agent({"127.0.0.1", server.port(), dir / "client"}, host);
    agent.start();
    EXPECT_TRUE(wait_for([&] { return host.current_version() == "2.0.0"; }));
}

TEST(EndToEnd, ServerRejectsOlderOrEqualVersions) {
    const auto dir = temp_dir("nx_e2e_order");
    nx::UpdateServer server({"127.0.0.1", 0, dir / "store"});
    ASSERT_TRUE(server.start());
    const std::string platform(nx::platform_id());
    std::string reply;
    ASSERT_TRUE(nx::publish_module("127.0.0.1", server.port(), "2.0.0", platform, NX_TEST_V2, reply));
    EXPECT_FALSE(nx::publish_module("127.0.0.1", server.port(), "2.0.0", platform, NX_TEST_V2, reply));
    EXPECT_FALSE(nx::publish_module("127.0.0.1", server.port(), "1.0.0", platform, NX_TEST_V1, reply));
    EXPECT_FALSE(nx::publish_module("127.0.0.1", server.port(), "not-a-version", platform, NX_TEST_V1, reply));
}

TEST(EndToEnd, ServerRestartServesStoredVersion) {
    const auto dir = temp_dir("nx_e2e_store");
    std::uint16_t port = 0;
    {
        nx::UpdateServer server({"127.0.0.1", 0, dir / "store"});
        ASSERT_TRUE(server.start());
        port = server.port();
        std::string reply;
        ASSERT_TRUE(nx::publish_module("127.0.0.1", port, "2.0.0", std::string(nx::platform_id()), NX_TEST_V2, reply));
    }
    nx::UpdateServer restarted({"127.0.0.1", 0, dir / "store"});
    ASSERT_TRUE(restarted.start());
    nx::ModuleHost host(dir / "client" / "runtime");
    ASSERT_TRUE(host.load(NX_TEST_V1));
    nx::UpdateAgent agent({"127.0.0.1", restarted.port(), dir / "client"}, host);
    agent.start();
    EXPECT_TRUE(wait_for([&] { return host.current_version() == "2.0.0"; }));
}

TEST(EndToEnd, ClientSurvivesServerAbsenceAndReconnects) {
    const auto dir = temp_dir("nx_e2e_reconnect");
    // Reserve a port, then free it: the client starts while nobody listens.
    std::uint16_t port = 0;
    {
        nx::UpdateServer probe({"127.0.0.1", 0, dir / "probe"});
        ASSERT_TRUE(probe.start());
        port = probe.port();
    }
    nx::ModuleHost host(dir / "client" / "runtime");
    ASSERT_TRUE(host.load(NX_TEST_V1));
    nx::UpdateAgent agent({"127.0.0.1", port, dir / "client"}, host);
    agent.start();
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(host.current_version(), "1.0.0");

    nx::UpdateServer server({"127.0.0.1", port, dir / "store"});
    ASSERT_TRUE(server.start());
    std::string reply;
    ASSERT_TRUE(nx::publish_module("127.0.0.1", port, "2.0.0", std::string(nx::platform_id()), NX_TEST_V2, reply));
    EXPECT_TRUE(wait_for([&] { return host.current_version() == "2.0.0"; }, 10000ms));
}
