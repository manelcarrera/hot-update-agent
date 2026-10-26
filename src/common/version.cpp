#include "nx/common.hpp"

#include <charconv>

namespace nx {

std::optional<Version> Version::parse(std::string_view text) {
    Version v;
    std::uint32_t* parts[] = {&v.major, &v.minor, &v.patch};
    const char* p = text.data();
    const char* end = text.data() + text.size();
    for (int i = 0; i < 3; ++i) {
        auto [next, ec] = std::from_chars(p, end, *parts[i]);
        if (ec != std::errc{} || next == p) return std::nullopt;
        p = next;
        if (i < 2) {
            if (p == end || *p != '.') return std::nullopt;
            ++p;
        }
    }
    if (p != end) return std::nullopt;
    return v;
}

std::string Version::str() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::string_view platform_id() {
#if defined(_WIN32)
#  define NX_OS "windows"
#elif defined(__APPLE__)
#  define NX_OS "macos"
#elif defined(__linux__)
#  define NX_OS "linux"
#else
#  define NX_OS "unknown"
#endif
#if defined(__x86_64__) || defined(_M_X64)
#  define NX_ARCH "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#  define NX_ARCH "arm64"
#else
#  define NX_ARCH "other"
#endif
    return NX_OS "-" NX_ARCH;
}

std::string_view shared_lib_extension() {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

}  // namespace nx
