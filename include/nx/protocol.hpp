#pragma once
// Wire protocol between client, server and publisher.
//
// Every message is a frame:  [u32 length][u8 type][payload]   (little-endian)
// where length counts the type byte plus the payload. Frames above kMaxFrame are
// rejected before any allocation, so a bad peer cannot make us allocate gigabytes.
//
// Flow (push, not polling):
//   client  -> HELLO{version, platform}
//   server  -> UPDATE_AVAILABLE{version, size, sha256}   (now, and whenever a new one is published)
//   client  -> GET{version}
//   server  -> MODULE{version, sha256, bytes}
//   publisher -> PUBLISH{version, platform, bytes};  server -> ACK | ERROR

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "nx/common.hpp"

namespace nx::proto {

inline constexpr std::uint32_t kMaxFrame = 64u * 1024u * 1024u;  // 64 MiB

enum class Type : std::uint8_t { Hello = 1, UpdateAvailable = 2, Get = 3, Module = 4, Publish = 5, Ack = 6, Error = 7 };

struct Hello { std::string version, platform; };
struct UpdateAvailable { std::string version; std::uint64_t size = 0; Digest sha{}; };
struct Get { std::string version; };
struct Module { std::string version; Digest sha{}; Bytes data; };
struct Publish { std::string version, platform; Bytes data; };
struct Ack { std::string text; };
struct Error { std::string text; };

using Message = std::variant<Hello, UpdateAvailable, Get, Module, Publish, Ack, Error>;

// Whole frame (header included), ready to send.
Bytes encode(const Message& m);
// Decodes the part after the length: [type][payload]. nullopt if malformed.
std::optional<Message> decode(std::span<const std::uint8_t> body);

}  // namespace nx::proto
