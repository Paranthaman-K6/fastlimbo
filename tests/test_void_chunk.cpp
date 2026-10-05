#include <cassert>
#include <cstdio>
#include "world/void_chunk.h"

int main() {
  using namespace limbo::world;
  // Non-empty body for a modern PVN.
  auto body = buildVoidChunkBody(767, 0, 0);
  assert(!body.empty());
  // Non-empty body for a legacy PVN.
  auto legacy = buildVoidChunkBody(47, 0, 0);
  assert(!legacy.empty());
  printf("test_void_chunk ok\n");
  return 0;
}
