// nx-publish: uploads a new version of the functional part.
//   nx-publish --version 2.0.0 --file functional_v2.so [--platform linux-x86_64] [--host H] [--port P]
#include <cstdio>
#include <cstring>
#include <string>

#include "nx/common.hpp"
#include "nx/publisher.hpp"

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", version, file, platform(nx::platform_id());
    std::uint16_t port = 47800;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--host")) host = argv[i + 1];
        else if (!std::strcmp(argv[i], "--port")) port = static_cast<std::uint16_t>(std::stoi(argv[i + 1]));
        else if (!std::strcmp(argv[i], "--version")) version = argv[i + 1];
        else if (!std::strcmp(argv[i], "--file")) file = argv[i + 1];
        else if (!std::strcmp(argv[i], "--platform")) platform = argv[i + 1];
    }
    if (version.empty() || file.empty()) {
        std::fprintf(stderr, "usage: nx-publish --version X.Y.Z --file <library> [--platform P] [--host H] [--port P]\n");
        return 2;
    }
    std::string reply;
    const bool ok = nx::publish_module(host, port, version, platform, file, reply);
    std::printf("%s: %s\n", ok ? "ok" : "error", reply.c_str());
    return ok ? 0 : 1;
}
