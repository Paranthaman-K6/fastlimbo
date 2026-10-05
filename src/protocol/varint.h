#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace limbo::proto {

// Encode signed 32-bit as Minecraft VarInt (two's complement, LE 7-bit groups).
inline void writeVarInt(std::vector<uint8_t>& out, int32_t v) {
  uint32_t u = static_cast<uint32_t>(v);
  do {
    uint8_t b = u & 0x7F;
    u >>= 7;
    if (u) b |= 0x80;
    out.push_back(b);
  } while (u);
}

// Decode VarInt from buf at pos; advances pos. Returns nullopt on truncation/overflow (>5 bytes).
inline std::optional<int32_t> readVarInt(const uint8_t* buf, size_t len, size_t& pos) {
  uint32_t val = 0;
  int shift = 0;
  for (int i = 0; i < 5; ++i) {
    if (pos >= len) return std::nullopt;
    uint8_t b = buf[pos++];
    val |= static_cast<uint32_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) return static_cast<int32_t>(val);
    shift += 7;
  }
  return std::nullopt;  // >5 bytes: malformed
}

inline std::optional<int32_t> readVarInt(const std::vector<uint8_t>& buf, size_t& pos) {
  return readVarInt(buf.data(), buf.size(), pos);
}

}  // namespace limbo::proto
