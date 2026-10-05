#include <cassert>
#include <cstdio>
#include "protocol/buffer.h"

int main() {
  using namespace limbo::proto;
  Writer w;
  w.varInt(300); w.str("hi"); w.i32(-5); w.i64(1234567890123LL); w.boolean(true);
  w.u16(25565); w.f32(1.5f);
  Reader r(w.b);
  assert(r.varInt() == 300);
  assert(r.str() == "hi");
  assert(r.i32() == -5);
  assert(r.i64() == 1234567890123LL);
  assert(r.boolean() == true);
  assert(r.u16() == 25565);
  assert(r.f32() == 1.5f);
  assert(r.ok && r.remaining() == 0);

  // String bound enforced.
  Writer w2; w2.varInt(99999); w2.bytes((const uint8_t*)"x", 1);
  Reader r2(w2.b);
  (void)r2.str(10);
  assert(!r2.ok);

  // framePacket round-trip: len+id+body.
  std::vector<uint8_t> body = {0xAA, 0xBB};
  auto f = framePacket(0x00, body);
  Reader fr(f);
  int32_t len = fr.varInt();
  assert(fr.ok && len == (int32_t)(1 + body.size()));
  assert(fr.varInt() == 0x00);
  auto rest = fr.bytes(fr.remaining());
  assert(rest == body);

  // offline UUID stable + version/variant bits.
  auto a = offlineUuid("Notch"), b = offlineUuid("Notch");
  assert(a == b);
  assert((a[6] & 0xF0) == 0x30 && (a[8] & 0xC0) == 0x80);
  printf("test_buffer ok\n");
  return 0;
}
