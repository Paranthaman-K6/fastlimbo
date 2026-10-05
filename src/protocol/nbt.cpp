// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "protocol/nbt.h"

#include <cstring>

namespace limbo::nbt {

void NbtWriter::u16(uint16_t v) {
  b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void NbtWriter::i32(int32_t v) {
  for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>((static_cast<uint32_t>(v) >> (i * 8)) & 0xFF));
}

void NbtWriter::i64(int64_t v) {
  uint64_t u = static_cast<uint64_t>(v);
  for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
}

void NbtWriter::f32(float v) {
  uint32_t u = 0;
  std::memcpy(&u, &v, sizeof u);
  i32(static_cast<int32_t>(u));
}

void NbtWriter::f64(double v) {
  uint64_t u = 0;
  std::memcpy(&u, &v, sizeof u);
  i64(static_cast<int64_t>(u));
}

void NbtWriter::raw(const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }
void NbtWriter::raw(const uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); }

void NbtWriter::tag(uint8_t type, const std::string& name) {
  u8(type);
  u16(static_cast<uint16_t>(name.size()));
  raw(name);
}

void NbtWriter::byteTag(const std::string& n, int8_t v) {
  tag(TAG_Byte, n);
  u8(static_cast<uint8_t>(v));
}

void NbtWriter::shortTag(const std::string& n, int16_t v) {
  tag(TAG_Short, n);
  u16(static_cast<uint16_t>(v));
}

void NbtWriter::intTag(const std::string& n, int32_t v) {
  tag(TAG_Int, n);
  i32(v);
}

void NbtWriter::longTag(const std::string& n, int64_t v) {
  tag(TAG_Long, n);
  i64(v);
}

void NbtWriter::floatTag(const std::string& n, float v) {
  tag(TAG_Float, n);
  f32(v);
}

void NbtWriter::doubleTag(const std::string& n, double v) {
  tag(TAG_Double, n);
  f64(v);
}

void NbtWriter::stringTag(const std::string& n, const std::string& v) {
  tag(TAG_String, n);
  u16(static_cast<uint16_t>(v.size()));
  raw(v);
}

void NbtWriter::byteArrayTag(const std::string& n, const std::vector<int8_t>& v) {
  tag(TAG_ByteArray, n);
  i32(static_cast<int32_t>(v.size()));
  for (int8_t x : v) u8(static_cast<uint8_t>(x));
}

void NbtWriter::intArrayTag(const std::string& n, const std::vector<int32_t>& v) {
  tag(TAG_IntArray, n);
  i32(static_cast<int32_t>(v.size()));
  for (int32_t x : v) i32(x);
}

void NbtWriter::longArrayTag(const std::string& n, const std::vector<int64_t>& v) {
  tag(TAG_LongArray, n);
  i32(static_cast<int32_t>(v.size()));
  for (int64_t x : v) i64(x);
}

void NbtWriter::beginCompound(const std::string& n) { tag(TAG_Compound, n); }

void NbtWriter::beginRootCompound() { u8(TAG_Compound); }  // anonymous root (1.20.2+)

void NbtWriter::endCompound() { u8(TAG_End); }

void NbtWriter::beginList(uint8_t elemType, const std::string& n, int32_t count) {
  tag(TAG_List, n);
  u8(elemType);
  i32(count);
}

void NbtWriter::writeAnonymousString(const std::string& s) {
  u8(TAG_String);
  u16(static_cast<uint16_t>(s.size()));
  raw(s);
}

std::vector<uint8_t> anonymousString(const std::string& s) {
  NbtWriter w;
  w.writeAnonymousString(s);
  return w.b;
}

}  // namespace limbo::nbt
