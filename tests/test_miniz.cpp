// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

// miniz inflate verification: python-zlib-generated fixtures (stored, fixed,
// dynamic Huffman, gzip + zlib wrappers) must round-trip byte-exact.
// Fixtures in tests/data/ (*.bin = expected output).
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "miniz.h"

static std::vector<uint8_t> load(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

int main() {
  struct Case {
    const char* comp;
    const char* expect;
  };
  const Case cases[] = {
      {"tests/data/stored.z", "tests/data/stored.bin"},
      {"tests/data/fixed.z", "tests/data/fixed.bin"},
      {"tests/data/dyn.z", "tests/data/dyn.bin"},
      {"tests/data/rand.bin.gz", "tests/data/rand.bin"},
      {"tests/data/rand.bin.z", "tests/data/rand.bin"},
  };
  for (const auto& c : cases) {
    auto comp = load(c.comp);
    auto want = load(c.expect);
    assert(!comp.empty() && !want.empty());
    std::vector<uint8_t> out;
    size_t n = 0;
    assert(mi::gunzip(comp.data(), comp.size(), out, n) && out == want);
  }
  // Corrupt input must fail, never crash.
  auto bad = load("tests/data/rand.bin.gz");
  bad[10] ^= 0xFF;
  std::vector<uint8_t> out;
  size_t n = 0;
  assert(!mi::gunzip(bad.data(), bad.size(), out, n));
  // Truncated input must fail.
  auto trunc = load("tests/data/dyn.z");
  trunc.resize(trunc.size() / 2);
  assert(!mi::gunzip(trunc.data(), trunc.size(), out, n));
  printf("test_miniz ok\n");
  return 0;
}
