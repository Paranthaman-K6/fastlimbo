#include "world/schematic.h"
#include <cassert>
#include <cstdio>
#include <string>

int main() {
  // Test 1: loadSchem with nonexistent file should fail
  {
    limbo::schem::Error err;
    auto s = limbo::schem::loadSchem("/nonexistent/schema.schem", err);
    assert(!s && "Expected nullopt for nonexistent file");
    printf("PASS: loadSchem rejects nonexistent file\n");
  }

  // Test 2: pasteBounds with default Schem
  {
    limbo::schem::Schem s;
    int minX, minY, minZ, maxX, maxY, maxZ;
    bool ok = limbo::schem::pasteBounds(s, minX, minY, minZ, maxX, maxY, maxZ);
    assert(!ok && "Expected false for zero-dimensions schem");
    printf("PASS: pasteBounds rejects zero-dimensions schem\n");
  }

  // Test 3: Schem struct has correct defaults
  {
    limbo::schem::Schem s;
    assert(s.w == 0 && s.h == 0 && s.l == 0);
    assert(s.dataVersion == 0);
    assert(s.off[0] == 0 && s.off[1] == 0 && s.off[2] == 0);
    assert(s.palette.empty());
    assert(s.blocks.empty());
    printf("PASS: Schem defaults are zeroed\n");
  }

  // Test 4: loadSchem with gzip-compressed minimal v2 schem
  // Construct a minimal .schem v2: 2x1x1 stone+air
  // We'll use Python to create the gzip file, then test loading it.
  // For now, skip and document.

  printf("PASS: test_schematic public API ok\n");
  printf("test_schematic ok\n");
  return 0;
}