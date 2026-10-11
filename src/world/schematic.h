// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once

#include <cstdint>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace limbo::schem {

// NBT tag types (standard big-endian NBT ids).
enum class NbtType : uint8_t {
  End   = 0,
  Byte  = 1,
  Short = 2,
  Int   = 3,
  Long  = 4,
  Float = 5,
  Double = 6,
  ByteArray = 7,
  String = 8,
  List = 9,
  Compound = 10,
  IntArray = 11,
  LongArray = 12
};

// -------------------------------------------------------------------
// .schem v2/v3 format summary
//   File = gzip( NBT compound )
//   Required NBT fields (v2):
//     Width  (Int,  -X or +X direction)
//     Height (Int,  -Y or +Y direction, usually positive)
//     Length (Int,  -Z or +Z direction)
//     Palette   (Compound: name -> id as Byte)
//     Blocks    (ByteArray: palette index per cell, w*h*l bytes)
//     DataVersion (Int): 2 or 3
//     Offset    (Long  : signed x,y,z offset from world origin)
//   v3 adds a nested "Blocks" compound structure; this reader rejects
//   v3 that isn't trivially resolvable (documented).
//
//  Unknown block names in the palette are kept as-preserved strings;
//  mapping to protocol block IDs is the lead's responsibility.
//
//  Crash safety: all bounds-checked, fuzz-friendly. No allocations
//  based on untrusted dimensions beyond the caps below.
//
// -------------------------------------------------------------------

struct Schem {
  int w = 0;   // width (X)
  int h = 0;   // height (Y)
  int l = 0;   // length (Z)
  int version = 0;  // Sponge Version (2 or 3)
  int dataVersion = 0;
  std::array<int, 3> off = {0, 0, 0}; // offset {x, y, z}

  // Palette: name -> id string. Index order matches block storage.
  std::vector<std::string> palette;       // palette[i] = name of block i
  std::vector<int> paletteIds;            // paletteIds[i] = numeric id (may be -1 if unnamed)

  // Block indices for each cell in row-major order (w * h * l total).
  // Each value is an index into palette (0 .. palette.size()-1),
  // or -1 if the palette is empty/missing.
  std::vector<int> blocks;
};

// Error message output parameter; empty on success.
using Error = std::string;

// Load a .schem file from disk.  gzip-decompresses then parses NBT.
// On success returns optional<Schem> with data; on failure returns nullopt
// and sets err.
std::optional<Schem> loadSchem(const std::string& path, Error& err);

// Helper: given a Schem, compute the bounds of the region it covers.
// Returns true if the schem has valid dimensions.
bool pasteBounds(const Schem& s, int& minX, int& minY, int& minZ,
                 int& maxX, int& maxY, int& maxZ);

} // namespace limbo::schem