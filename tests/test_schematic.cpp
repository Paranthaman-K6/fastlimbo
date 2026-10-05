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

  // Test 4: load the real spec-compliant fixture (tools/gen_spawn_schem.py).
  {
    limbo::schem::Error err;
    auto s = limbo::schem::loadSchem("tests/data/spawn.schem", err);
    assert(s && "spawn.schem must load");
    assert(s->w == 8 && s->h == 3 && s->l == 8);
    assert(s->palette.size() == 3);
    assert(s->palette[0] == "minecraft:air" && s->palette[1] == "minecraft:stone" &&
           s->palette[2] == "minecraft:glass");
    assert(s->blocks.size() == 8 * 3 * 8);
    auto at = [&](int x, int y, int z) { return s->blocks[(y * s->l + z) * s->w + x]; };
    assert(at(0, 0, 0) == 1 && at(7, 0, 7) == 1);  // stone floor
    assert(at(0, 1, 0) == 2 && at(7, 1, 7) == 2);  // glass pillars
    assert(at(1, 1, 1) == 0 && at(4, 2, 4) == 0);  // air
    int ax, ay, az, bx, by, bz;
    assert(limbo::schem::pasteBounds(*s, ax, ay, az, bx, by, bz));
    assert(ax == 0 && ay == 0 && az == 0 && bx == 7 && by == 2 && bz == 7);
    printf("PASS: spawn.schem loads with correct content\n");
  }

  // Test 5: corrupt gzip must fail, not crash.
  {
    limbo::schem::Error err;
    auto s = limbo::schem::loadSchem("tests/data/rand.bin.gz", err);
    assert(!s && "random gzip is not a schem");
    printf("PASS: corrupt input rejected\n");
  }

  printf("PASS: test_schematic public API ok\n");
  printf("test_schematic ok\n");
  return 0;
}