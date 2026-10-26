#include "nx/common.hpp"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace nx {

void log(std::string_view component, std::string_view message) {
    static std::mutex mutex;
    using namespace std::chrono;
    const auto now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    const std::lock_guard lock(mutex);
    std::fprintf(stderr, "%lld.%03lld [%.*s] %.*s\n", static_cast<long long>(now / 1000 % 100000),
                 static_cast<long long>(now % 1000), static_cast<int>(component.size()), component.data(),
                 static_cast<int>(message.size()), message.data());
    std::fflush(stderr);
}

}  // namespace nx
