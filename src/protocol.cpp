#include "gpudirectfabric/protocol.hpp"

#include <cstring>

namespace gpudirectfabric {

namespace {
std::uint32_t crc32(const std::uint8_t* data, std::size_t len) noexcept {
  static std::uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

void append_u16(std::vector<std::uint8_t>& b, std::uint16_t v) {
  b.push_back(static_cast<std::uint8_t>(v));
  b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void append_u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
}  // namespace

const char* to_string(MessageType v) noexcept {
  switch (v) {
    case MessageType::HELLO: return "HELLO";
    case MessageType::REGISTER_WORKER: return "REGISTER_WORKER";
    case MessageType::PUBLISH_DEVICE: return "PUBLISH_DEVICE";
    case MessageType::PUBLISH_ENDPOINT: return "PUBLISH_ENDPOINT";
    case MessageType::PUBLISH_CAPABILITY: return "PUBLISH_CAPABILITY";
    case MessageType::REGISTER_BUFFER: return "REGISTER_BUFFER";
    case MessageType::READY: return "READY";
    case MessageType::CREATE_BUFFER: return "CREATE_BUFFER";
    case MessageType::ACK: return "ACK";
    case MessageType::NACK: return "NACK";
    case MessageType::QUERY_STATE: return "QUERY_STATE";
    default: return "UNKNOWN";
  }
}

void pw_u8(std::vector<std::uint8_t>& b, std::uint8_t v) { b.push_back(v); }
void pw_u16(std::vector<std::uint8_t>& b, std::uint16_t v) { append_u16(b, v); }
void pw_u32(std::vector<std::uint8_t>& b, std::uint32_t v) { append_u32(b, v); }
void pw_u64(std::vector<std::uint8_t>& b, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void pw_str(std::vector<std::uint8_t>& b, const std::string& s) {
  append_u32(b, static_cast<std::uint32_t>(s.size()));
  for (char ch : s) b.push_back(static_cast<std::uint8_t>(ch));
}

std::uint8_t PayloadReader::u8() {
  if (off + 1 > n) { fail = true; return 0; }
  return p[off++];
}
std::uint16_t PayloadReader::u16() {
  if (off + 2 > n) { fail = true; return 0; }
  std::uint16_t v = static_cast<std::uint16_t>(p[off]) | (static_cast<std::uint16_t>(p[off + 1]) << 8);
  off += 2;
  return v;
}
std::uint32_t PayloadReader::u32() {
  if (off + 4 > n) { fail = true; return 0; }
  std::uint32_t v = static_cast<std::uint32_t>(p[off]) | (static_cast<std::uint32_t>(p[off + 1]) << 8) |
                    (static_cast<std::uint32_t>(p[off + 2]) << 16) | (static_cast<std::uint32_t>(p[off + 3]) << 24);
  off += 4;
  return v;
}
std::uint64_t PayloadReader::u64() {
  if (off + 8 > n) { fail = true; return 0; }
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[off + i]) << (8 * i);
  off += 8;
  return v;
}
bool PayloadReader::str(std::string& out) {
  std::uint32_t len = u32();
  if (fail || len > 1u << 20 || off + len > n) { fail = true; return false; }
  out.assign(reinterpret_cast<const char*>(p + off), len);
  off += len;
  return true;
}
bool PayloadReader::read_bytes(void* dst, std::size_t len) {
  if (off + len > n) { fail = true; return false; }
  std::memcpy(dst, p + off, len);
  off += len;
  return true;
}

std::vector<std::uint8_t> encode_frame(const Frame& frame) {
  std::vector<std::uint8_t> b;
  b.reserve(kProtoHeaderSize + frame.payload.size());
  b.push_back('G'); b.push_back('D'); b.push_back('F'); b.push_back('1');
  append_u16(b, kProtoVersion);
  b.push_back(static_cast<std::uint8_t>(frame.type));
  b.push_back(frame.flags);
  append_u32(b, static_cast<std::uint32_t>(frame.payload.size()));
  b.insert(b.end(), frame.payload.begin(), frame.payload.end());
  std::uint32_t crc = crc32(b.data(), b.size());
  append_u32(b, crc);
  return b;
}

DecodeResult decode_frame(const std::vector<std::uint8_t>& wire) {
  DecodeResult r;
  if (wire.size() < kProtoHeaderSize) {
    r.error = "frame too short (truncated)";
    return r;
  }
  if (wire[0] != 'G' || wire[1] != 'D' || wire[2] != 'F' || wire[3] != '1') {
    r.error = "bad magic";
    return r;
  }
  std::uint16_t version = static_cast<std::uint16_t>(wire[4]) | (static_cast<std::uint16_t>(wire[5]) << 8);
  if (version != kProtoVersion) {
    r.error = "unsupported protocol version";
    return r;
  }
  std::uint32_t payload_len = static_cast<std::uint32_t>(wire[8]) | (static_cast<std::uint32_t>(wire[9]) << 8) |
                              (static_cast<std::uint32_t>(wire[10]) << 16) | (static_cast<std::uint32_t>(wire[11]) << 24);
  if (payload_len > kProtoMaxPayload) {
    r.error = "absurd payload length";
    return r;
  }
  if (wire.size() != kProtoHeaderSize + payload_len) {
    r.error = "frame length mismatch";
    return r;
  }
  const std::size_t crc_off = wire.size() - 4;
  std::uint32_t stored_crc = static_cast<std::uint32_t>(wire[crc_off]) |
                             (static_cast<std::uint32_t>(wire[crc_off + 1]) << 8) |
                             (static_cast<std::uint32_t>(wire[crc_off + 2]) << 16) |
                             (static_cast<std::uint32_t>(wire[crc_off + 3]) << 24);
  std::uint32_t calc = crc32(wire.data(), wire.size() - 4);
  if (stored_crc != calc) {
    r.error = "invalid checksum";
    return r;
  }
  std::uint8_t typeval = wire[6];
  r.frame.flags = wire[7];
  // The payload begins at offset 12 (magic4+ver2+type1+flags1+len4 = 12), then crc 4.
  r.frame.payload.assign(wire.begin() + 12, wire.end() - 4);
  switch (typeval) {
    case 1: r.frame.type = MessageType::HELLO; break;
    case 2: r.frame.type = MessageType::REGISTER_WORKER; break;
    case 3: r.frame.type = MessageType::PUBLISH_DEVICE; break;
    case 4: r.frame.type = MessageType::PUBLISH_ENDPOINT; break;
    case 5: r.frame.type = MessageType::PUBLISH_CAPABILITY; break;
    case 6: r.frame.type = MessageType::REGISTER_BUFFER; break;
    case 7: r.frame.type = MessageType::READY; break;
    case 9: r.frame.type = MessageType::CREATE_BUFFER; break;
    case 40: r.frame.type = MessageType::ACK; break;
    case 41: r.frame.type = MessageType::NACK; break;
    case 8: r.frame.type = MessageType::QUERY_STATE; break;
    default:
      r.error = "malformed message type";
      return r;
  }
  r.ok = true;
  return r;
}

}  // namespace gpudirectfabric