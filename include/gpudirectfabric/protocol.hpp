#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gpudirectfabric {

// Bounded framed transfer protocol.
//
// Frame on the wire:
//   magic[4]  = "GDF1"
//   version   = u16 (1)
//   type      = u8
//   flags     = u8
//   payload_len = u32
//   crc32     = u32 (over magic..payload, i.e. everything before crc)
//   payload   = payload_len bytes
//
// Rejects: bad magic, unsupported version, truncated frames, invalid checksum,
// absurd lengths, malformed enum values (validated on decode).

constexpr std::uint16_t kProtoVersion = 1;
constexpr std::uint32_t kProtoMaxPayload = 1u << 20;  // 1 MiB
constexpr std::uint32_t kProtoHeaderSize = 16;

enum class MessageType : std::uint8_t {
  HELLO = 1,
  REGISTER_WORKER = 2,
  PUBLISH_DEVICE = 3,
  PUBLISH_ENDPOINT = 4,
  PUBLISH_CAPABILITY = 5,
  REGISTER_BUFFER = 6,
  READY = 7,
  CREATE_BUFFER = 9,
  ACK = 40,
  NACK = 41,
  QUERY_STATE = 8
};

const char* to_string(MessageType v) noexcept;

struct Frame {
  MessageType type = MessageType::HELLO;
  std::uint8_t flags = 0;
  std::vector<std::uint8_t> payload;
};

// Encode a frame (returns the complete wire frame). Never fails for a bounded payload.
std::vector<std::uint8_t> encode_frame(const Frame& frame);

struct DecodeResult {
  bool ok = false;
  Frame frame;
  std::string error;
};

// Decode a complete wire frame (already read including payload). Validates magic,
// version, enum, length and CRC.
DecodeResult decode_frame(const std::vector<std::uint8_t>& wire);

// ---- Payload helpers ----
void pw_u8(std::vector<std::uint8_t>& b, std::uint8_t v);
void pw_u16(std::vector<std::uint8_t>& b, std::uint16_t v);
void pw_u32(std::vector<std::uint8_t>& b, std::uint32_t v);
void pw_u64(std::vector<std::uint8_t>& b, std::uint64_t v);
void pw_str(std::vector<std::uint8_t>& b, const std::string& s);

// Streaming reader with strict bounds.
struct PayloadReader {
  const std::uint8_t* p;
  std::size_t n;
  std::size_t off = 0;
  bool fail = false;
  std::uint8_t u8();
  std::uint16_t u16();
  std::uint32_t u32();
  std::uint64_t u64();
  bool str(std::string& out);
  bool read_bytes(void* dst, std::size_t len);
};

}  // namespace gpudirectfabric