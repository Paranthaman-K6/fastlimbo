# Schematic Support

> **Status:** 🟡 In progress — tracked on the internal task board.
> **Spec:** [Sponge Schematic Specification v2](https://github.com/SpongePowered/Schematic-Specification)

---

## Scope

| Format | Status | Reason |
|--------|--------|--------|
| **Sponge `.schem` v2** | 🟡 Primary target | Modern, documented, zlib+NBT, palette+BlockData varints, WE/FAWE export |
| `.schematic` (MCEdit legacy) | 🔲 Deferred | Legacy IDs, ambiguous `AddBlocks` extended IDs |
| `.litematic` (Litematica) | 🔲 Deferred | Region entities, pending ticks, separate spec drift |
| `.mca` (Anvil region) | 🔲 Deferred | Full region/chunk/heightmap/light reconstruction |

**Decision:** the Sponge `.schem` v2 reader targets the narrow case only — a single pasted schematic at spawn, inflated with the bundled [`third_party/miniz`](https://github.com/Paranthaman-K6/limbo/tree/main/third_party) deflate implementation. Full block palettes and NBT-driven schematics are explicitly out of scope.

---

## Configuration

```properties
# Optional: enable schematic paste at spawn
schematic_path=/data/lobby.schem
schematic_offset_x=0
schematic_offset_y=0
schematic_offset_z=0
```

- If `schematic_path` is empty or file unreadable → **void fallback** (always works)
- Offsets are in **blocks** (not chunks)
- Schematic origin (0,0,0) maps to `spawn + offset`

---

## Sponge `.schem` v2 Format

### File Structure

```
schem file (gzip)
└── NBT Compound (unnamed)
    ├── "Width" (Short)     — X size (blocks)
    ├── "Height" (Short)    — Y size (blocks)
    ├── "Length" (Short)    — Z size (blocks)
    ├── "Palette" (List)    — Block state palette
    │   └── Compound per entry
    │       ├── "Name" (String) — e.g. "minecraft:stone"
    │       └── "Properties" (Compound) — state properties
    ├── "BlockData" (ByteArray) — Palette indices (VarInt-packed)
    ├── "Entities" (List)   — Optional, ignored by limbo
    ├── "TileEntities" (List) — Optional, ignored
    └── "Metadata" (Compound) — Optional (author, description, etc.)
```

### Palette Encoding

- Palette size ≤ 256 → 1 byte per block (direct index)
- Palette size ≤ 4096 → 2 bytes (12-bit VarInt per block)
- Palette size > 4096 → 4 bytes (32-bit per block)
- **limbo-c++ reads all three; writes single palette per chunk section**

### BlockData Layout

```
BlockData = concatenated chunk sections (16×16×16)
Each section: 4096 blocks × bitsPerEntry bits (packed)
Order: Y (0→15), Z (0→15), X (0→15)  — same as vanilla chunk
```

---

## Paste Algorithm

```cpp
// Pseudocode
void pasteSchematic(const Schematic& schem, const Vec3& origin) {
    for (int y = 0; y < schem.height; ++y) {
        for (int z = 0; z < schem.length; ++z) {
            for (int x = 0; x < schem.width; ++x) {
                BlockState state = schem.getBlock(x, y, z);
                if (state.isAir()) continue;

                int wx = origin.x + x;
                int wy = origin.y + y;
                int wz = origin.z + z;

                int cx = wx >> 4;  // chunk X
                int cz = wz >> 4;  // chunk Z
                Chunk& chunk = getOrCreateChunk(cx, cz);
                chunk.setBlock(wx & 15, wy, wz & 15, state);
            }
        }
    }
    // After paste: send all non-empty chunks in Play burst
}
```

**Chunk sending:** Only chunks with non-air blocks are sent. Void chunks fill gaps.

---

## Size & Safety Limits (Planned)

| Limit | Value | Enforcement |
|-------|-------|-------------|
| Max chunks | 4096 (16×16×16) | Reject at load |
| Max decoded NBT | 64 MiB | `miniz` inflate with cap |
| Max palette entries | 8192 | Reject at parse |
| Parse timeout | 5 seconds | Worker thread + watchdog |
| Memory limit | 256 MiB | RSS check post-load |

**Violations → log error, fall back to void world.**

---

## Implementation Plan

### Phase 1: Reader (Agent C)

| File | Responsibility |
|------|----------------|
| `third_party/miniz.h/.c` | Vendored zlib (single-file, no sudo) |
| `src/world/schematic.h/.cpp` | `.schem` v2 parser → `Schematic` struct |
| `tests/test_schematic.cpp` | Unit tests (round-trip, size caps, malformed) |

### Phase 2: Integration (Lead Merge)

1. Load schematic at startup (config `schematic_path`)
2. On Play join, if schematic loaded → paste at spawn+offset
3. Send pasted chunks instead of / alongside void chunk
4. Fallback to void on any error

### Phase 3: Testing

| Test | Tool |
|------|------|
| Valid `.schem` paste | `matrix.py --schematic` |
| Oversize reject | `fuzz_suite.py` |
| Malformed gzip/NBT | `fuzz_suite.py` |
| Void fallback | `matrix.py` (no schematic) |

---

## Miniz Integration

```cpp
// third_party/miniz.h — single header, C API
// Used for gzip decompression only
extern "C" {
    #include "miniz.h"
}

// Decompress gzip → raw NBT
std::vector<uint8_t> decompressGzip(const std::vector<uint8_t>& gz) {
    mz_stream stream = {};
    stream.next_in = const_cast<uint8_t*>(gz.data());
    stream.avail_in = gz.size();
    // ... mz_inflateInit2(&stream, MZ_DEFAULT_WINDOW_BITS | 32) for gzip
    // ... mz_inflate(&stream, MZ_FINISH)
    // ... mz_inflateEnd(&stream)
}
```

**No zlib dev package needed** — `miniz.c` compiled directly.

---

## Testing Schematics

### Generate Test Schematic (WorldEdit/FAWE)

```bash
# In-game (with WorldEdit/FAWE)
/schem save lobby
# Exports to plugins/WorldEdit/schematics/lobby.schem
```

### Validate with limbo-c++

```sh
# Once implemented
./build/test_schematic /path/to/lobby.schem
# → prints dimensions, palette size, block count, chunk count
```

---

## Related

- [Void World](Void-World.md) — fallback when no schematic
- [Configuration](Configuration.md) — `schematic_path`, offsets
- [Hardening & Security](Hardening-Security.md) — size caps, parse limits
- [Schematic Specification](https://github.com/SpongePowered/Schematic-Specification) — upstream format spec
- [Sponge Schematic Spec](https://github.com/SpongePowered/Schematic-Specification)