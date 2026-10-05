// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include <cassert>
#include <cstdio>
#include <vector>

#include "protocol/nbt.h"

using limbo::nbt::NbtWriter;
using limbo::nbt::TAG_Int;
using Bytes = std::vector<uint8_t>;

int main() {
  // 1. Anonymous TAG_String layout (1.20.3+ TextComponent / registry optionals):
  //    [0x08][u16 len][bytes] — no name. Byte-exact.
  Bytes expectHello = {0x08, 0x00, 0x05, 'h', 'e', 'l', 'l', 'o'};
  assert(limbo::nbt::anonymousString("hello") == expectHello);

  NbtWriter a;
  a.writeAnonymousString("hi");
  assert(a.b == (Bytes{0x08, 0x00, 0x02, 'h', 'i'}));

  // 2. Named TAG_String: [0x08][u16 name len][name][u16 val len][value].
  NbtWriter s;
  s.stringTag("id", "x");
  assert(s.b == (Bytes{0x08, 0x00, 0x02, 'i', 'd', 0x00, 0x01, 'x'}));

  // 3. Anonymous root compound (network NBT, 1.20.2+): [0x0A] payload [0x00].
  NbtWriter c;
  c.beginRootCompound();
  c.stringTag("k", "v");
  c.endCompound();
  assert(c.b == (Bytes{0x0A, 0x08, 0x00, 0x01, 'k', 0x00, 0x01, 'v', 0x00}));

  // 4. Named child compound nests anonymously: [0x0A][name] payload [0x00].
  NbtWriter n;
  n.beginCompound("sub");
  n.endCompound();
  assert(n.b == (Bytes{0x0A, 0x00, 0x03, 's', 'u', 'b', 0x00}));

  // 5. Big-endian Int, including negative (two's complement).
  NbtWriter i;
  i.intTag("n", -5);
  assert(i.b == (Bytes{0x03, 0x00, 0x01, 'n', 0xFF, 0xFF, 0xFF, 0xFB}));

  // 6. Long big-endian: 1 -> 8 bytes.
  NbtWriter l;
  l.longTag("v", 1);
  assert(l.b == (Bytes{0x04, 0x00, 0x01, 'v', 0, 0, 0, 0, 0, 0, 0, 1}));

  // 7. LongArray: [0x0C][name][i32 count][8*count bytes].
  NbtWriter la;
  la.longArrayTag("x", {1, -1});
  Bytes wantLa = {0x0C, 0x00, 0x01, 'x', 0x00, 0x00, 0x00, 0x02};
  for (int k = 0; k < 8; ++k) wantLa.push_back(0x00);
  wantLa.back() = 0x01;
  for (int k = 0; k < 8; ++k) wantLa.push_back(0xFF);
  assert(la.b == wantLa);

  // 8. List header: [0x09][name][elem type][i32 count] then raw elements.
  NbtWriter li;
  li.beginList(TAG_Int, "v", 2);
  li.i32(7);
  li.i32(9);
  assert(li.b == (Bytes{0x09, 0x00, 0x01, 'v', 0x03, 0x00, 0x00, 0x00, 0x02,
                        0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x09}));

  // 9. Float/Double bit patterns (big-endian IEEE-754): 1.0f -> 0x3F800000.
  NbtWriter f;
  f.floatTag("f", 1.0f);
  assert(f.b == (Bytes{0x05, 0x00, 0x01, 'f', 0x3F, 0x80, 0x00, 0x00}));

  // 10. ByteArray count prefix is big-endian i32.
  NbtWriter ba;
  ba.byteArrayTag("b", {1, 2});
  assert(ba.b == (Bytes{0x07, 0x00, 0x01, 'b', 0x00, 0x00, 0x00, 0x02, 0x01, 0x02}));

  printf("test_nbt ok\n");
  return 0;
}
