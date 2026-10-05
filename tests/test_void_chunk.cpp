// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

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
