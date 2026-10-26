#include "nx/common.hpp"

#include <fstream>
#include <system_error>

namespace nx {

std::optional<Bytes> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) return false;
    }
    std::filesystem::rename(tmp, path, ec);  // atomic on POSIX; replaces on Windows
    return !ec;
}

}  // namespace nx
