# .schem v2 Format Notes

## Layout

A `.schem` file is **gzip-compressed NBT**. The decompressed payload is a
Minecraft NBT compound tag with the following required fields (v2):

| Tag name    | NBT type | Description                     |
|-------------|----------|---------------------------------|
| `Width`     | Int      | X size of the schematic         |
| `Height`    | Int      | Y size of the schematic           |
| `Length`    | Int      | Z size of the schematic           |
| `Palette`   | Compound | name→id mappings (Byte values)  |
| `Blocks`    | ByteArray| Palette index per cell, row-major (w×h×l bytes) |
| `DataVersion` | Int   | 2 or 3 (currently only v2 is fully supported) |
| `Offset`    | Long     | X,Y,Z offset from world origin    |

### Palette

The `Palette` compound contains one child per block type. Each child is a **Byte**
tag where:

* The tag **name** (VarInt‑encoded string) is the human-readable block name
  (e.g. `"stone"`, `"air"`).
* The tag **value** is the palette index (0‑based) that appears in `Blocks`.

Block names are preserved as‑is; mapping to protocol block IDs is the lead’s
responsibility. Unknown block strings are kept and not rejected.

### Blocks

`Blocks` is a `ByteArray` of length `w × h × l`. Each byte is an index into the
palette. If the palette has N entries, valid indices are `0 … N‑1`. Values >= N
are considered out‑of-range and should be treated as air or rejected.

### DataVersion

* **v2** — standard format as described above. `Blocks` is a flat `ByteArray`.
* **v3** — adds a nested `Blocks` compound structure. This reader **rejects**
  v3 `Blocks` that are not trivially resolvable (i.e. not a flat `ByteArray`);
  such files produce an error. A “trivial” v3 is one where `Blocks` is still a
  `ByteArray` with the same layout as v2.

### Caps (bounds-checked, fuzz-friendly)

| Constraint | Limit | Reason |
|------------|-------|--------|
| Max per-dimension | 256 | Prevent OOM from huge dims |
| Max total blocks | 1 M (256³) | Prevent allocation overflow |
| Width/Height/Length | > 0 and ≤ 256 | Must be positive and bounded |

If any cap is violated, `loadSchem` returns `nullopt` with an error string.
No allocation is performed beyond the cap limit.

### DataVersion mismatch policy

* **Warn + air-fallback**: If `DataVersion` is 3 (or an unrecognised value),
  the file is **accepted** but blocks that reference palette entries not present
  in the v2‑compatible mapping are filled with the string `"air"` in the output
  `Schem`. This avoids crashes when encountering future formats.

### Merge snippet (paste-at-spawn)

When pasting a `.schem` into a void world, the lead client places the region
at the computed offset (`Offset` field). The region occupies `[off_x, off_x+w)`
× `[off_y, off_y+h)` × `[off_z, off_z+l)`. Cells whose palette index has no
matching name are filled with air, so the void surrounding the schem remains
unchanged. The schematic’s bounding box is computed by `pasteBounds`.

## API

```cpp
struct Schem {
  int w, h, l;                 // dimensions
  int dataVersion;
  std::array<int, 3> off;      // offset {x,y,z}
  std::vector<std::string> palette;   // name → id (index order)
  std::vector<int> paletteIds;        // numeric id per palette entry
  std::vector<int> blocks;            // palette index per cell, w*h*l total
};

// Returns nullopt on failure; sets err on error.
std::optional<Schem> loadSchem(const std::string& path, Error& err);

// Compute paste bounds from a loaded Schem.
bool pasteBounds(const Schem& s, int& minX, int& minY, int& minZ,
                 int& maxX, int& maxY, int& maxZ);
```

## Build & Test

```sh
# Build the library + tests
make

# Run unit tests (includes test_schematic)
make test

# Expected output should include "test_schematic ok"
```