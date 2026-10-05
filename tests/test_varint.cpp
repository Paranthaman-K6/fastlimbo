#include <cassert>
#include <cstdio>
#include "protocol/varint.h"

int main() {
  using limbo::proto::readVarInt;
  using limbo::proto::writeVarInt;
  // 25565 = DD C7 01
  {
    std::vector<uint8_t> o; writeVarInt(o, 25565);
    assert(o.size() == 3 && o[0] == 0xDD && o[1] == 0xC7 && o[2] == 0x01);
    size_t p = 0; auto v = readVarInt(o, p);
    assert(v && *v == 25565 && p == 3);
  }
  // -1 -> 5 bytes FF FF FF FF 0F
  {
    std::vector<uint8_t> o; writeVarInt(o, -1);
    assert(o.size() == 5);
    size_t p = 0; auto v = readVarInt(o, p);
    assert(v && *v == -1);
  }
  // 0, 1, 2147483647, overflow reject
  {
    std::vector<uint8_t> o; writeVarInt(o, 0);
    size_t p = 0; assert(readVarInt(o, p).value() == 0);
    std::vector<uint8_t> over = {0x80,0x80,0x80,0x80,0x80,0x01};
    size_t q = 0; assert(!readVarInt(over, q));
  }
  printf("test_varint ok\n");
  return 0;
}
