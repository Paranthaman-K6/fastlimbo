#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace limbo::proto {

// Big-endian writer for Minecraft wire format.
struct Writer {
  std::vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void boolean(bool v) { b.push_back(v ? 1 : 0); }
  void i16(int16_t v) { b.push_back((v >> 8) & 0xFF); b.push_back(v & 0xFF); }
  void u16(uint16_t v) { b.push_back((v >> 8) & 0xFF); b.push_back(v & 0xFF); }
  void i32(int32_t v) {
    b.push_back((v >> 24) & 0xFF); b.push_back((v >> 16) & 0xFF);
    b.push_back((v >> 8) & 0xFF); b.push_back(v & 0xFF);
  }
  void i64(int64_t v) {
    for (int i = 7; i >= 0; --i) b.push_back((v >> (i * 8)) & 0xFF);
  }
  void f32(float v);
  void f64(double v);
  void varInt(int32_t v);
  void varLong(int64_t v);
  void str(const std::string& s);          // VarInt byte-len + UTF-8 (no 32767 enforce here; caller caps)
  void bytes(const uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); }
  void bytes(const std::vector<uint8_t>& v) { b.insert(b.end(), v.begin(), v.end()); }
  void uuid(const std::array<uint8_t, 16>& u) { bytes(u.data(), 16); }
};

// Bounds-checked reader. `ok` becomes false on any overrun; callers check.
struct Reader {
  const uint8_t* p;
  size_t n;
  size_t pos = 0;
  bool ok = true;
  Reader(const uint8_t* p_, size_t n_) : p(p_), n(n_) {}
  explicit Reader(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
  uint8_t u8();
  bool boolean();
  int16_t i16();
  uint16_t u16();
  int32_t i32();
  int64_t i64();
  float f32();
  double f64();
  int32_t varInt();
  int64_t varLong();
  std::string str(size_t maxBytes = 32767 * 3 + 3);  // returns "" + ok=false on violation
  std::vector<uint8_t> bytes(size_t count);
  std::array<uint8_t, 16> uuid();
  size_t remaining() const { return pos <= n ? n - pos : 0; }
};

// Minecraft packet framing: [VarInt len][VarInt id][body]. len covers id+body.
std::vector<uint8_t> framePacket(int32_t packetId, const std::vector<uint8_t>& body);

// Offline-mode UUID: UUIDv3("OfflinePlayer:"+name) per vanilla.
std::array<uint8_t, 16> offlineUuid(const std::string& name);

}  // namespace limbo::proto
