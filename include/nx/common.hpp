#pragma once
// Small shared pieces: SHA-256, semantic versions, platform id, logging, files.

#include <array>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nx {

using Bytes = std::vector<std::uint8_t>;

// --- SHA-256 (integrity of the downloaded module) ------------------------------
using Digest = std::array<std::uint8_t, 32>;
Digest sha256(std::span<const std::uint8_t> data);
std::string to_hex(std::span<const std::uint8_t> data);

// --- Semantic version MAJOR.MINOR.PATCH ------------------------------------------
struct Version {
    std::uint32_t major = 0, minor = 0, patch = 0;
    static std::optional<Version> parse(std::string_view text);
    std::string str() const;
    auto operator<=>(const Version&) const = default;
};

// --- Platform id ("linux-x86_64", "windows-x86_64", "macos-arm64") -------------
// A module is only offered to clients of the same platform.
std::string_view platform_id();
// File extension of a shared library on this platform (".so", ".dll", ".dylib").
std::string_view shared_lib_extension();

// --- Logging (thread-safe, one line per call) ----------------------------------
void log(std::string_view component, std::string_view message);

// --- Files ---------------------------------------------------------------------
std::optional<Bytes> read_file(const std::filesystem::path& path);
// Writes to a temporary file in the same directory and renames it into place, so a
// reader never sees a half-written file.
bool write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> data);

}  // namespace nx
