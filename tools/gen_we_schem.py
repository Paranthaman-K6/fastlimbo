# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""Emit real WorldEdit-style Sponge .schem fixtures.

Shapes are byte-faithful to WorldEdit's own writers (verified against
EngineHub/WorldEdit sources on 2026-10-05):

  SpongeSchematicV3Writer.write():
      root entry name ""  -> compound "Schematic" {
          Version: Int 3, DataVersion: Int,
          Metadata: { Date: Long, WorldEdit: { Version: String,
              EditingPlatform: String, Origin: IntArray,
              Platforms: { <id>: {Name,Version} } } },
          Width/Height/Length: Short, Offset: IntArray,
          Blocks: { Palette: {state -> Int}, Data: ByteArray(varint),
                    BlockEntities: List<Compound> },
          Biomes:  { Palette: {biome -> Int}, Data: ByteArray(varint) },
      }

  SpongeSchematicV2Writer.write():
      root entry name "Schematic" -> compound {
          Version: Int 2, DataVersion: Int, Metadata: {...},
          Width/Height/Length: Short, Offset: IntArray,
          PaletteMax: Int, Palette: {state -> Int},
          BlockData: ByteArray(varint), BlockEntities: List<Compound>,
          BiomePaletteMax/BiomePalette/BiomeData (optional),
      }

Outputs (gzip-compressed NBT, big-endian):
  tests/data/we_v3.schem         WorldEdit v3 shape, 8x3x8 (same pattern as
                                 spawn.schem: stone floor, glass corners)
  tests/data/we_v2.schem         WorldEdit v2 shape, 8x3x8, same pattern
  tests/data/we_v2blocks.schem   legacy flat "Blocks" ByteArray shape, 4x2x4
  tests/data/we_cap.schem        Width Short 300 (> dim cap), must be rejected
"""
import gzip
import os
import struct
import sys


def tag(t, name, payload):
    return struct.pack(">B", t) + struct.pack(">H", len(name)) + name.encode() + payload


def end():
    return b"\x00"


def nbt_str(s):
    b = s.encode()
    return struct.pack(">H", len(b)) + b


def t_int(name, v):
    return tag(3, name, struct.pack(">i", v))


def t_short(name, v):
    return tag(2, name, struct.pack(">h", v))


def t_long(name, v):
    return tag(4, name, struct.pack(">q", v))


def t_string(name, s):
    return tag(8, name, nbt_str(s))


def t_compound(name, inner):
    return tag(10, name, inner + end())


def t_int_array(name, vals):
    body = struct.pack(">i", len(vals)) + b"".join(struct.pack(">i", v) for v in vals)
    return tag(11, name, body)


def t_byte_array(name, body):
    return tag(7, name, struct.pack(">i", len(body)) + body)


def t_list(name, elem_type, items):
    body = struct.pack(">B", elem_type) + struct.pack(">i", len(items)) + b"".join(items)
    return tag(9, name, body)


def t_root(root_name, inner):
    return struct.pack(">B", 10) + struct.pack(">H", len(root_name)) + root_name.encode() + inner + end()


def varint(n):
    out = b""
    u = n & 0xFFFFFFFF
    while True:
        b = u & 0x7F
        u >>= 7
        out += bytes([b | 0x80 if u else b])
        if not u:
            break
    return out


def spawn_cells(w, h, l):
    """Same pattern as tools/gen_spawn_schem.py: stone floor, glass corners."""
    cells = []
    for y in range(h):
        for z in range(l):
            for x in range(w):
                if y == 0:
                    cells.append("minecraft:stone")
                elif y == 1 and x in (0, w - 1) and z in (0, l - 1):
                    cells.append("minecraft:glass")
                else:
                    cells.append("minecraft:air")
    return cells


def first_seen_palette(names):
    """WorldEdit assigns palette ids in first-seen order (Object2IntLinkedOpenHashMap)."""
    palette = {}
    ids = []
    for n in names:
        if n not in palette:
            palette[n] = len(palette)
        ids.append(palette[n])
    return palette, ids


def metadata_compound(date_ms=1727500000000):
    """Metadata exactly as SpongeSchematicV3Writer writes it."""
    platforms = t_compound("bukkit", t_string("Name", "Paper") + t_string("Version", "1.21.4"))
    we = (t_string("Version", "7.3.1") + t_string("EditingPlatform", "bukkit") +
          t_int_array("Origin", [0, 64, 0]) + t_compound("Platforms", platforms))
    return t_compound("Metadata", t_long("Date", date_ms) + t_compound("WorldEdit", we))


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(gzip.compress(data))
    print(path, "written")


def gen_v3(path, w, h, l):
    cells = spawn_cells(w, h, l)
    palette, ids = first_seen_palette(cells)
    data = b"".join(varint(i) for i in ids)

    palette_comp = b"".join(t_int(k, v) for k, v in palette.items())  # t_compound ends it
    blocks = (t_compound("Palette", palette_comp) +
              t_byte_array("Data", data) +
              t_list("BlockEntities", 10, []))

    biome_palette = t_compound("Palette", t_int("minecraft:plains", 0))
    biome_data = b"\x00" * (w * h * l)

    schematic = (t_int("Version", 3) + t_int("DataVersion", 4189) +
                 metadata_compound() +
                 t_short("Width", w) + t_short("Height", h) + t_short("Length", l) +
                 t_int_array("Offset", [4, -60, 4]) +
                 t_compound("Blocks", blocks) +
                 t_compound("Biomes", biome_palette + t_byte_array("Data", biome_data)))
    write(path, t_root("", t_compound("Schematic", schematic)))


def gen_v2(path, w, h, l):
    cells = spawn_cells(w, h, l)
    palette, ids = first_seen_palette(cells)
    data = b"".join(varint(i) for i in ids)

    palette_comp = b"".join(t_int(k, v) for k, v in palette.items())  # t_compound ends it
    we = (t_string("Version", "7.3.1") + t_string("EditingPlatform", "bukkit") +
          t_int_array("Offset", [0, 0, 0]) +
          t_compound("Platforms", t_compound("bukkit", t_string("Name", "Paper") +
                                             t_string("Version", "1.21.4"))))
    meta = (t_int("WEOffsetX", 0) + t_int("WEOffsetY", 0) + t_int("WEOffsetZ", 0) +
            t_compound("WorldEdit", we))

    schematic = (t_int("Version", 2) + t_int("DataVersion", 4189) +
                 t_compound("Metadata", meta) +
                 t_short("Width", w) + t_short("Height", h) + t_short("Length", l) +
                 t_int_array("Offset", [4, -60, 4]) +
                 t_int("PaletteMax", len(palette)) +
                 t_compound("Palette", palette_comp) +
                 t_byte_array("BlockData", data) +
                 t_list("BlockEntities", 10, []))
    # Root entry NAME is "Schematic" (SpongeSchematicV2Writer.write).
    write(path, t_root("Schematic", schematic))


def gen_v2_blocks(path, w, h, l):
    """Legacy flat shape: Palette compound + raw 'Blocks' ByteArray (one byte
    per cell, palette index < 128)."""
    cells = spawn_cells(w, h, l)
    palette, ids = first_seen_palette(cells)
    palette_comp = b"".join(t_int(k, v) for k, v in palette.items())  # t_compound ends it
    schematic = (t_int("Version", 2) + t_int("DataVersion", 2860) +
                 t_short("Width", w) + t_short("Height", h) + t_short("Length", l) +
                 t_int_array("Offset", [0, 0, 0]) +
                 t_compound("Palette", palette_comp) +
                 t_byte_array("Blocks", bytes(ids)))
    write(path, t_root("", schematic))


def gen_cap(path):
    """Width Short 300 > kMaxDim (256): must be rejected before big reads."""
    w, h, l = 300, 1, 1
    schematic = (t_int("Version", 3) + t_int("DataVersion", 4189) +
                 metadata_compound() +
                 t_short("Width", w) + t_short("Height", h) + t_short("Length", l) +
                 t_int_array("Offset", [0, 0, 0]) +
                 t_compound("Blocks",
                            t_compound("Palette", t_int("minecraft:air", 0)) +
                            t_byte_array("Data", b"\x00" * (w * h * l)) +
                            t_list("BlockEntities", 10, [])))
    write(path, t_root("", t_compound("Schematic", schematic)))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/data"
    gen_v3(os.path.join(out, "we_v3.schem"), 8, 3, 8)
    gen_v2(os.path.join(out, "we_v2.schem"), 8, 3, 8)
    gen_v2_blocks(os.path.join(out, "we_v2blocks.schem"), 4, 2, 4)
    gen_cap(os.path.join(out, "we_cap.schem"))


if __name__ == "__main__":
    main()
