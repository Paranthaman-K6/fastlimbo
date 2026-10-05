# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""Regenerate tests/data/spawn.schem (spec-compliant Sponge v2, 8x3x8).
Stone floor + glass corner pillars, air elsewhere. Palette: air=0, stone=1, glass=2."""
import struct, gzip, os
os.makedirs("tests/data", exist_ok=True)
def tag(t, name, payload):
    return struct.pack(">B", t) + struct.pack(">H", len(name)) + name + payload
def varint(n):
    out = b""; u = n & 0xFFFFFFFF
    while True:
        b = u & 0x7F; u >>= 7
        out += bytes([b | 0x80 if u else b])
        if not u: break
    return out
W, H, L = 8, 3, 8
palette = {"minecraft:air": 0, "minecraft:stone": 1, "minecraft:glass": 2}
cells = []
for y in range(H):
    for z in range(L):
        for x in range(W):
            if y == 0: cells.append(1)
            elif y == 1 and x in (0, W-1) and z in (0, L-1): cells.append(2)
            else: cells.append(0)
blockdata = b"".join(varint(c) for c in cells)
pal = b"".join(tag(3, k.encode(), struct.pack(">i", v)) for k, v in palette.items()) + b"\x00"
root = b""
root += tag(3, b"Version", struct.pack(">i", 2))
root += tag(3, b"DataVersion", struct.pack(">i", 3700))
root += tag(2, b"Width", struct.pack(">h", W))
root += tag(2, b"Height", struct.pack(">h", H))
root += tag(2, b"Length", struct.pack(">h", L))
root += tag(10, b"Palette", pal)
root += tag(7, b"BlockData", struct.pack(">i", len(blockdata)) + blockdata)
root += tag(11, b"Offset", struct.pack(">i", 3) + struct.pack(">iii", 0, 0, 0))
root += b"\x00"
nbt = struct.pack(">B", 10) + struct.pack(">H", 0) + root
open("tests/data/spawn.schem", "wb").write(gzip.compress(nbt))
print("tests/data/spawn.schem written")
