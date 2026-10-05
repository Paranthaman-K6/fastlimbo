// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "protocol/buffer.h"
#include <cstring>
#include "protocol/varint.h"
// OpenSSL MD5 for offline UUID (UUIDv3). EVP_Q_digest avoids the deprecated MD5() API.
#include <openssl/evp.h>

namespace limbo::proto {

void Writer::f32(float v) {
  uint32_t u; std::memcpy(&u, &v, 4);
  i32(static_cast<int32_t>(u));
}
void Writer::f64(double v) {
  uint64_t u; std::memcpy(&u, &v, 8);
  for (int i = 7; i >= 0; --i) b.push_back((u >> (i * 8)) & 0xFF);
}
void Writer::varInt(int32_t v) { writeVarInt(b, v); }
void Writer::varLong(int64_t v) {
  uint64_t u = static_cast<uint64_t>(v);
  do {
    uint8_t t = u & 0x7F; u >>= 7;
    if (u) t |= 0x80;
    b.push_back(t);
  } while (u);
}
void Writer::str(const std::string& s) {
  varInt(static_cast<int32_t>(s.size()));
  bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

uint8_t Reader::u8() {
  if (pos >= n) { ok = false; return 0; }
  return p[pos++];
}
bool Reader::boolean() { return u8() != 0; }
int16_t Reader::i16() { return static_cast<int16_t>((u16())); }
uint16_t Reader::u16() {
  if (pos + 2 > n) { ok = false; return 0; }
  uint16_t v = (static_cast<uint16_t>(p[pos]) << 8) | p[pos + 1];
  pos += 2; return v;
}
int32_t Reader::i32() {
  if (pos + 4 > n) { ok = false; return 0; }
  int32_t v = (static_cast<int32_t>(p[pos]) << 24) | (static_cast<int32_t>(p[pos+1]) << 16) |
              (static_cast<int32_t>(p[pos+2]) << 8) | p[pos+3];
  pos += 4; return v;
}
int64_t Reader::i64() {
  if (pos + 8 > n) { ok = false; return 0; }
  int64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[pos + i];
  pos += 8; return v;
}
float Reader::f32() { uint32_t u = static_cast<uint32_t>(i32()); float f; std::memcpy(&f, &u, 4); return f; }
double Reader::f64() {
  if (pos + 8 > n) { ok = false; return 0; }
  uint64_t u = 0;
  for (int i = 0; i < 8; ++i) u = (u << 8) | p[pos + i];
  pos += 8; double d; std::memcpy(&d, &u, 8); return d;
}
int32_t Reader::varInt() {
  auto r = readVarInt(p, n, pos);
  if (!r) { ok = false; return 0; }
  return *r;
}
int64_t Reader::varLong() {
  uint64_t v = 0; int shift = 0;
  for (int i = 0; i < 10; ++i) {
    if (pos >= n) { ok = false; return 0; }
    uint8_t t = p[pos++];
    v |= static_cast<uint64_t>(t & 0x7F) << shift;
    if (!(t & 0x80)) return static_cast<int64_t>(v);
    shift += 7;
  }
  ok = false; return 0;
}
std::string Reader::str(size_t maxBytes) {
  int32_t len = varInt();
  if (!ok || len < 0 || static_cast<size_t>(len) > maxBytes || static_cast<size_t>(len) > remaining()) {
    ok = false; return {};
  }
  std::string s(reinterpret_cast<const char*>(p + pos), len);
  pos += len;
  return s;
}
std::vector<uint8_t> Reader::bytes(size_t count) {
  if (count > remaining()) { ok = false; return {}; }
  std::vector<uint8_t> v(p + pos, p + pos + count);
  pos += count; return v;
}
std::array<uint8_t, 16> Reader::uuid() {
  std::array<uint8_t, 16> u{};
  if (remaining() < 16) { ok = false; return u; }
  std::memcpy(u.data(), p + pos, 16); pos += 16; return u;
}

std::vector<uint8_t> framePacket(int32_t packetId, const std::vector<uint8_t>& body) {
  std::vector<uint8_t> id; writeVarInt(id, packetId);
  std::vector<uint8_t> out; writeVarInt(out, static_cast<int32_t>(id.size() + body.size()));
  out.insert(out.end(), id.begin(), id.end());
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

std::array<uint8_t, 16> offlineUuid(const std::string& name) {
  std::string in = "OfflinePlayer:" + name;
  unsigned char digest[16];
  size_t outLen = 0;
  if (!EVP_Q_digest(nullptr, "MD5", nullptr, in.data(), in.size(), digest, &outLen) || outLen != 16) {
    // Practically unreachable (MD5 fetch from default provider); zero-UUID fallback.
    std::array<uint8_t, 16> z{};
    return z;
  }
  std::array<uint8_t, 16> u;
  std::memcpy(u.data(), digest, 16);
  u[6] = (u[6] & 0x0F) | 0x30;  // version 3
  u[8] = (u[8] & 0x3F) | 0x80;  // variant RFC4122
  return u;
}

}  // namespace limbo::proto
