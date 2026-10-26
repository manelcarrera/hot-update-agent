#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace nx {

// Uploads a new functional part to the server. Returns true on ACK; `reply` gets
// the server's ACK or ERROR text.
bool publish_module(const std::string& host, std::uint16_t port, const std::string& version,
                    const std::string& platform, const std::filesystem::path& library, std::string& reply);

}  // namespace nx
