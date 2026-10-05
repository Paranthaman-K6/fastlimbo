#pragma once
// Minimal big-endian NBT *writer* (no reader needed: we only emit payloads).
// Used by Configuration RegistryData (1.20.2+) and modern TextComponent NBT.
//
// Two flavours matter on the wire:
//  - classic named root:  [0x0A][u16 name len][name][payload][TAG_End]
//  - network "anonymous" root (1.20.2+): [0x0A][payload][TAG_End]  <- beginRootCompound()
// Nested compounds inside a parent are always anonymous (type byte + payload only),
// which is what beginCompound()/endCompound() produce.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace limbo::nbt {

// NBT tag ids (classic NBT spec).
enum : uint8_t {
  TAG_End = 0,
  TAG_Byte = 1,
  TAG_Short = 2,
  TAG_Int = 3,
  TAG_Long = 4,
  TAG_Float = 5,
  TAG_Double = 6,
  TAG_ByteArray = 7,
  TAG_String = 8,
  TAG_List = 9,
  TAG_Compound = 10,
  TAG_IntArray = 11,
  TAG_LongArray = 12,
};

struct NbtWriter {
  std::vector<uint8_t> b;

  // --- raw big-endian primitives (shared with proto::Writer semantics) ---
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v);
  void i32(int32_t v);
  void i64(int64_t v);
  void f32(float v);
  void f64(double v);
  void raw(const std::string& s);
  void raw(const uint8_t* p, size_t n);

  // --- tag header helpers ---
  void tag(uint8_t type, const std::string& name);  // id byte + u16-len name

  // --- named scalar tags ---
  void byteTag(const std::string& n, int8_t v);
  void shortTag(const std::string& n, int16_t v);
  void intTag(const std::string& n, int32_t v);
  void longTag(const std::string& n, int64_t v);
  void floatTag(const std::string& n, float v);
  void doubleTag(const std::string& n, double v);
  void stringTag(const std::string& n, const std::string& v);

  // --- named array tags ---
  void byteArrayTag(const std::string& n, const std::vector<int8_t>& v);
  void intArrayTag(const std::string& n, const std::vector<int32_t>& v);
  void longArrayTag(const std::string& n, const std::vector<int64_t>& v);

  // --- structure ---
  // Named child compound: [0x0A][name] ... [0x00]. Call endCompound() to close.
  void beginCompound(const std::string& n);
  // Anonymous network root compound (1.20.2+): [0x0A] ... [0x00].
  void beginRootCompound();
  void endCompound();  // TAG_End
  // Named list: [0x09][name][elem type][i32 count]; elements are written raw
  // afterwards (compound list elements = payload + TAG_End, no type/name).
  void beginList(uint8_t elemType, const std::string& n, int32_t count);

  // Anonymous TAG_String (1.20.3+ chat components): [0x08][u16 len][bytes].
  void writeAnonymousString(const std::string& s);
};

// Convenience: bytes of an anonymous TAG_String (unit asserts / call sites).
std::vector<uint8_t> anonymousString(const std::string& s);

}  // namespace limbo::nbt
