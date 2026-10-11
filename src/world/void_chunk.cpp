// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "world/void_chunk.h"

#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "protocol/buffer.h"
#include "protocol/codec_data.h"
#include "protocol/versions.h"
#include "world/schematic.h"

// Chunk bodies verified 2026-10-05 against minecraft-data protocol.json
// (per-minor dirs). Sections for 1.18+ always present (24, overworld);
// bitmask eras send zero sections (absent = air). Full-bright via
// emptySky/emptyBlock masks (all sections empty => skylight max).

namespace {

// Minimal inline NBT: TAG_Compound(10) + optional u16(0) root name, one
// TAG_Long_Array(12) "MOTION_BLOCKING" i32(37) + 37 zero i64, TAG_End(0).
// Pre-1.21.5 heightmap layout. "nbt" fields (<=1.20.1) carry the empty root
// name; "anonymousNbt" fields (1.20.2+) omit it.
std::vector<uint8_t> heightmapNbt(bool anonymous) {
  limbo::proto::Writer w;
  w.u8(10);
  if (!anonymous) w.u16(0);
  w.u8(12);
  const char* kName = "MOTION_BLOCKING";  // 15 chars
  w.u16(15);
  w.bytes(reinterpret_cast<const uint8_t*>(kName), 15);
  w.i32(37);
  for (int i = 0; i < 37; ++i) w.i64(0);
  w.u8(0);
  return w.b;
}

// Empty named compound (1.14/1.15 heightmaps when unused).
std::vector<uint8_t> emptyNbt() { return {0x0A, 0x00, 0x00, 0x00}; }

// One empty 1.18+ section: blockCount 0, single air palette, single biome.
// BPE=0 single-valued palettes encode as: BPE(0) + paletteID(VarInt) +
// dataLength(VarInt=0). NO palette-length VarInt (Via PaletteType1_18.write).
void emptySection(limbo::proto::Writer& w, int biomeIdx) {
  w.i16(0);      // block count
  w.u8(0);       // blocks BPE: single-valued
  w.varInt(0);   // air (global id 0, stable since 1.13 flattening)
  w.varInt(0);   // data array length
  w.u8(0);       // biomes BPE: single-valued
  w.varInt(biomeIdx);
  w.varInt(0);
}

// 1.18+ section with schematic overlay: BPE=4 linear palette [air, stone].
// 4096 cells / 16 per long = 256 longs; cell (y*16+z)*16+x -> long c/16,
// nibble (c%16)*4. Matches Via PaletteType1_18.write (min 4 bits for block
// palettes) + vanilla BitStorage packing. Solid = non-air palette name.
void pasteSection(limbo::proto::Writer& w, int biomeIdx, const uint64_t bits[64],
                  bool anySolid) {
  int blocks = 0;
  for (int i = 0; i < 64; ++i) blocks += __builtin_popcountll(bits[i]);
  if (!anySolid || blocks == 0) {
    emptySection(w, biomeIdx);
    return;
  }
  // Expand the 1-bit-per-cell bitmap to 4-bit-per-cell (BPE=4).
  uint64_t packed[256] = {};
  for (int c = 0; c < 4096; ++c) {
    if (bits[c / 64] & (1ULL << (c % 64))) {
      packed[c / 16] |= 1ULL << ((c % 16) * 4);
    }
  }
  w.i16(static_cast<int16_t>(blocks));
  w.u8(4);  // BPE 4 (linear block palette minimum)
  w.varInt(2);
  w.varInt(0);  // air
  w.varInt(1);  // stone (global id 1, stable since 1.13 flattening)
  w.varInt(256);
  for (int i = 0; i < 256; ++i) w.i64(static_cast<int64_t>(packed[i]));
  w.u8(0);  // biomes BPE: single-valued (no palette length)
  w.varInt(biomeIdx);
  w.varInt(0);
}

bool schemSolid(const limbo::schem::Schem& s, int idx) {
  if (idx < 0 || idx >= (int)s.blocks.size()) return false;
  int pi = s.blocks[idx];
  if (pi < 0 || pi >= (int)s.palette.size()) return false;
  const std::string& name = s.palette[pi];
  return name != "minecraft:air" && name != "minecraft:cave_air" &&
         name != "minecraft:void_air" && name != "air" && !name.empty();
}

// Full-bright sky light: one 2048-byte 0xFF array per section.
// Empty light masks mean ZERO light (black) per the wiki ("each set bit
// indicates all zeros"), so every lit chunk must ship real arrays.
void writeFullBrightSky(limbo::proto::Writer& w) {
  static const std::vector<uint8_t> full(2048, 0xFF);
  w.varInt(26);
  for (int i = 0; i < 26; ++i) w.bytes(full);
}

// Legacy full-chunk biome arrays (<=1.14) live INSIDE the size-prefixed data
// blob: 1.12 = 256 raw bytes, 1.13/1.14 = 256 big-endian i32s (plains = 1).
// Via ChunkType1_9_3/1_13/1_14 readers consume them unconditionally when
// fullChunk is set; omitting them desyncs Via and hard-fails native clients.
void legacyChunk(limbo::proto::Writer& w, int biomeElemBytes) {
  w.boolean(true);  // ground-up continuous
  w.varInt(0);      // no sections (absent = air)
  w.varInt(256 * biomeElemBytes);  // data blob = biomes only (void)
  if (biomeElemBytes == 1) {
    for (int i = 0; i < 256; ++i) w.u8(1);  // plains
  } else {
    for (int i = 0; i < 256; ++i) w.i32(1);  // plains
  }
  w.varInt(0);  // block entities
}

}  // namespace

namespace limbo::world {

namespace {
std::optional<schem::Schem> g_spawn;
std::mutex g_spawn_mu;
bool g_spawn_ok = false;  // true once a load succeeded
std::string g_spawn_loaded_path;  // path that produced g_spawn
std::string g_spawn_path;
}  // namespace

void setSpawnSchematic(const std::string& path) { g_spawn_path = path; }

// Retryable loader: a failed load can be retried (file appears later, config
// fixed) and the error always describes the latest attempt. A first failure
// no longer poisons all future loads (std::call_once did).
bool loadSpawnSchematic(std::string& err) {
  if (g_spawn_path.empty()) return true;  // void-only, not an error
  std::lock_guard<std::mutex> lk(g_spawn_mu);
  if (!g_spawn_ok || g_spawn_loaded_path != g_spawn_path) {
    schem::Error e;
    auto s = schem::loadSchem(g_spawn_path, e);
    if (!s) {
      g_spawn.reset();
      g_spawn_ok = false;
      err = "cannot load schematic '" + g_spawn_path + "': " + e +
            " (void fallback)";
      return false;
    }
    g_spawn = std::move(*s);
    g_spawn_ok = true;
    g_spawn_loaded_path = g_spawn_path;
  }
  err.clear();
  return true;
}

std::optional<PasteCtx> spawnPaste() {
  if (!g_spawn) return std::nullopt;
  PasteCtx ctx;
  ctx.schem = &*g_spawn;
  // Center on spawn block (8, *, 8); top of schem at y=319 (chunk sections
  // span -64..319, so a platform above that could never render).
  ctx.baseX = 8 - g_spawn->w / 2;
  ctx.baseY = 320 - g_spawn->h;
  ctx.baseZ = 8 - g_spawn->l / 2;
  return ctx;
}

double spawnYAt(double wx, double wz) {
  if (!g_spawn) return 400.0;  // void: safely above everything
  const schem::Schem& s = *g_spawn;
  int baseX = 8 - s.w / 2, baseY = 320 - s.h, baseZ = 8 - s.l / 2;
  // Feet on the highest solid under/near (wx,wz): scan a 7x7 column
  // footprint so a hole at the exact block still grounds the player.
  int best = -1;
  int cx0 = (int)floor(wx), cz0 = (int)floor(wz);
  for (int dz = -3; dz <= 3; ++dz)
    for (int dx = -3; dx <= 3; ++dx) {
      int sx = cx0 + dx - baseX, sz = cz0 + dz - baseZ;
      if (sx < 0 || sx >= s.w || sz < 0 || sz >= s.l) continue;
      for (int sy = s.h - 1; sy >= 0; --sy) {
        int idx = (sy * s.l + sz) * s.w + sx;
        if (schemSolid(s, idx)) {
          int wy = baseY + sy;
          if (wy > best) best = wy;
          break;
        }
      }
    }
  if (best >= 0) return (double)best + 1.0;
  // No solid near spawn (custom X/Z off the build): fall back to the
  // global file top (may float — same as the old behavior).
  for (int sy = s.h - 1; sy >= 0; --sy)
    for (int sz = 0; sz < s.l; ++sz)
      for (int sx = 0; sx < s.w; ++sx) {
        int idx = (sy * s.l + sz) * s.w + sx;
        if (schemSolid(s, idx)) return (double)(baseY + sy) + 1.0;
      }
  return (double)baseY + s.h;
}

double spawnY() { return spawnYAt(8.0, 8.0); }

namespace {
// Floor division for chunk mapping with negative coords.
int floorDiv16(int v) { return v >= 0 ? v / 16 : -((-v + 15) / 16); }
}  // namespace

void spawnChunkRange(int& minCx, int& maxCx, int& minCz, int& maxCz) {
  std::lock_guard<std::mutex> lk(g_spawn_mu);
  if (!g_spawn_ok || !g_spawn) {
    minCx = maxCx = minCz = maxCz = 0;  // void: player chunk only
    return;
  }
  const schem::Schem& s = *g_spawn;
  int baseX = 8 - s.w / 2, baseZ = 8 - s.l / 2;
  minCx = floorDiv16(baseX);
  maxCx = floorDiv16(baseX + s.w - 1);
  minCz = floorDiv16(baseZ);
  maxCz = floorDiv16(baseZ + s.l - 1);
}

std::vector<std::string> describeSpawnSchematic() {
  std::vector<std::string> out;
  char line[256];
  std::lock_guard<std::mutex> lk(g_spawn_mu);
  if (!g_spawn_ok || !g_spawn) return out;
  const schem::Schem& s = *g_spawn;
  struct stat st{};
  long fsize = (::stat(g_spawn_loaded_path.c_str(), &st) == 0) ? (long)st.st_size : -1;
  snprintf(line, sizeof(line), "file=%s (%ldB) sponge=v%d dataVersion=%d",
           g_spawn_loaded_path.c_str(), fsize, s.version, s.dataVersion);
  out.push_back(line);
  long total = (long)s.w * s.h * s.l;
  snprintf(line, sizeof(line), "dims=%dx%dx%d cells=%ld caps(d<=256,total<=1M,pal<=4096): %s",
           s.w, s.h, s.l, total,
           (s.w <= 256 && s.h <= 256 && s.l <= 256 && total <= 1024 * 1024 &&
            (int)s.palette.size() <= 4096)
               ? "ok"
               : "VIOLATION");
  out.push_back(line);
  // Palette histogram + solid/air split + per-section distinct states +
  // chunk coverage + dropped solids, in one cell pass.
  std::vector<size_t> hist(s.palette.size(), 0);
  size_t solid = 0;
  std::vector<std::set<int>> sectionStates(24);
  int minCx = 0, maxCx = 0, minCz = 0, maxCz = 0;
  bool firstCell = true;
  int baseX = 8 - s.w / 2, baseY = 320 - s.h, baseZ = 8 - s.l / 2;
  for (int sy = 0; sy < s.h; ++sy)
    for (int sz = 0; sz < s.l; ++sz)
      for (int sx = 0; sx < s.w; ++sx) {
        int idx = (sy * s.l + sz) * s.w + sx;
        int pi = (idx >= 0 && idx < (int)s.blocks.size()) ? s.blocks[idx] : -1;
        if (pi < 0 || pi >= (int)hist.size()) continue;
        hist[(size_t)pi]++;
        int wx = baseX + sx, wy = baseY + sy, wz = baseZ + sz;
        int cx = floorDiv16(wx), cz = floorDiv16(wz);
        if (firstCell) {
          minCx = maxCx = cx;
          minCz = maxCz = cz;
          firstCell = false;
        } else {
          if (cx < minCx) minCx = cx;
          if (cx > maxCx) maxCx = cx;
          if (cz < minCz) minCz = cz;
          if (cz > maxCz) maxCz = cz;
        }
        if (!schemSolid(s, idx)) continue;
        solid++;
        if (wy >= -64 && wy <= 319) sectionStates[(wy + 64) / 16].insert(pi);
      }
  size_t air = (size_t)total > solid ? (size_t)total - solid : 0;
  snprintf(line, sizeof(line), "blocks: total=%ld solid=%zu air=%zu palettes=%zu",
           total, solid, air, s.palette.size());
  out.push_back(line);
  // Top-5 palette entries by count.
  {
    std::string top;
    for (int n = 0; n < 5; ++n) {
      size_t best = 0;
      int bi = -1;
      for (size_t i = 0; i < hist.size(); ++i)
        if (hist[i] > best) {
          best = hist[i];
          bi = (int)i;
        }
      if (bi < 0 || best == 0) break;
      char e[160];
      snprintf(e, sizeof(e), "%s%s=%zu", top.empty() ? "" : ", ",
               s.palette[(size_t)bi].c_str(), best);
      top += e;
      hist[(size_t)bi] = 0;
    }
    snprintf(line, sizeof(line), "palette-top: %s", top.c_str());
    out.push_back(line);
  }
  snprintf(line, sizeof(line),
           "placement: base=(%d,%d,%d) (center 8,8; top=320-h) bounds x[%d,%d] y[%d,%d] z[%d,%d]%s",
           baseX, baseY, baseZ, baseX, baseX + s.w - 1, baseY, baseY + s.h - 1,
           baseZ, baseZ + s.l - 1,
           (s.off[0] || s.off[1] || s.off[2])
               ? " WARN: file Offset ignored (always centered)"
               : "");
  out.push_back(line);
  int clipLo18 = baseY < -64 ? -64 - baseY : 0;
  int clipHi18 = baseY + s.h - 1 > 319 ? baseY + s.h - 320 : 0;
  int clipLo17 = baseY < 0 ? -baseY : 0;
  int clipHi17 = baseY + s.h - 1 > 255 ? baseY + s.h - 256 : 0;
  snprintf(line, sizeof(line),
           "world-fit: 1.18+ rows clipped below=%d above=%d; <=1.17 below=%d above=%d",
           clipLo18, clipHi18, clipLo17, clipHi17);
  out.push_back(line);
  int nChunks = (maxCx - minCx + 1) * (maxCz - minCz + 1);
  snprintf(line, sizeof(line),
           "chunks: x[%d,%d] z[%d,%d] (%d total, all in login burst)",
           minCx, maxCx, minCz, maxCz, nChunks);
  out.push_back(line);
  snprintf(line, sizeof(line), "spawn: feet=%.1f (%s); biomes=plains; blockEntities=not loaded (0 sent)",
           spawnY(), g_spawn ? "on-platform" : "void");
  out.push_back(line);
  out.push_back("visibility: pvn>=757 shape-visible (non-air coerced to stone); "
                "755-756 + <=754 void (no sections sent)");
  // Per-section distinct states (BPE=4 fits <=16; flag overflow).
  {
    std::string over;
    for (int si = 0; si < 24; ++si) {
      if (sectionStates[(size_t)si].empty()) continue;
      char e[64];
      snprintf(e, sizeof(e), "%sy%d:%zu", over.empty() ? "" : " ",
               -64 + si * 16, sectionStates[(size_t)si].size());
      over += e;
      if (sectionStates[(size_t)si].size() > 16) over += "(!OVERFLOW)";
    }
    snprintf(line, sizeof(line), "sections(source-states; stone-coercion sends <=2): %s",
             over.empty() ? "(all empty)" : over.c_str());
    out.push_back(line);
  }
  if (solid == 0) out.push_back("WARN: all-air schematic (0 solid cells)");
  return out;
}

std::vector<uint8_t> buildVoidChunkBody(int pvn, int chunkX, int chunkZ) {
  return buildChunkBody(pvn, chunkX, chunkZ, PasteCtx{});
}

std::vector<uint8_t> buildChunkBody(int pvn, int chunkX, int chunkZ,
                                    const PasteCtx& ctx) {
  proto::Writer w;
  w.i32(chunkX);
  w.i32(chunkZ);
  proto::Era e = proto::eraForPvn(pvn);
  switch (e) {
    case proto::Era::LEGACY_1_8:
      // Map Chunk: ground-up, u16 bitmap, varint-prefixed data.
      w.boolean(true);
      w.u16(0);
      w.varInt(0);
      break;
    case proto::Era::V1_12:
      legacyChunk(w, 1);  // 1.12: 256 biome bytes inside the data blob
      break;
    case proto::Era::V1_13:
      legacyChunk(w, 4);  // 1.13: 256 biome i32s inside the data blob
      break;
    case proto::Era::V1_14:
      w.boolean(true);
      w.varInt(0);
      w.bytes(emptyNbt());
      w.varInt(1024);  // 1.14: 256 biome i32s after the heightmaps
      for (int i = 0; i < 256; ++i) w.i32(1);  // plains
      w.varInt(0);
      break;
    case proto::Era::V1_15:
      w.boolean(true);
      w.varInt(0);
      w.bytes(emptyNbt());
      for (int i = 0; i < 1024; ++i) w.i32(0);  // biomes (ground-up)
      w.varInt(0);
      w.varInt(0);
      break;
    case proto::Era::V1_16_0:
    case proto::Era::V1_16_2:
      w.boolean(true);   // ground-up
      if (e == proto::Era::V1_16_0) w.boolean(true);  // ignoreOldData (735/736 only)
      w.varInt(0);       // no sections (absent = air)
      w.bytes(heightmapNbt(false));  // "nbt": named root
      if (e == proto::Era::V1_16_0) {
        for (int i = 0; i < 1024; ++i) w.i32(0);  // biomes (fixed, ground-up)
      } else {
        w.varInt(1024);  // 1.16.2+: varint-counted biome array
        for (int i = 0; i < 1024; ++i) w.varInt(0);
      }
      w.varInt(0);       // chunk data length
      w.varInt(0);       // block entities
      break;
    case proto::Era::V1_17_1_18: {
      // 1.17: i64-array bitmask. 1.18+: full sections below.
      if (pvn < 757) {
        w.varInt(0);  // bitmask longs: none
        w.bytes(heightmapNbt(false));  // "nbt": named root
        // Full-chunk biome array: 16 sections x 64 (vanilla 1.17 height 256).
        // Must be 1024 entries, not empty: Via's 1_17_1->1_18 converter
        // indexes biomeData[i*64] unconditionally, and native clients read
        // the same array. Plains global id from the shipped j117 codec.
        w.varInt(1024);
        for (int i = 0; i < 1024; ++i) w.varInt(codec::plainsId("j117"));
        w.varInt(0);  // chunk data
        w.varInt(0);  // block entities
        break;
      }
      // fall through to 1.18+ sections
      [[fallthrough]];
    }
    default: {
      // 1.18+: x, z, heightmaps, sections buffer, entities, [trustEdges],
      // light masks (all empty => full bright), no light arrays.
      if (pvn >= 770) {
        // 1.21.5+: heightmaps are an array of {type, data}, not NBT.
        w.varInt(1);
        w.varInt(4);  // MOTION_BLOCKING
        w.varInt(37);
        for (int i = 0; i < 37; ++i) w.i64(0);
      } else {
        w.bytes(heightmapNbt(pvn >= 764));  // anonymousNbt from 1.20.2
      }
      proto::Writer sections;
      const bool overlay =
          (ctx.schem != nullptr) && (e != proto::Era::V1_16_0) &&
          (e != proto::Era::V1_16_2) && pvn >= 757;
      // Biome palette index: plains vanilla id for this target.
      int biomeIdx = 0;
      if (pvn <= 754) biomeIdx = 1;  // 1.16 and below: plains
      else if (pvn <= 756) biomeIdx = codec::plainsId("j117");
      else if (pvn <= 758) biomeIdx = codec::plainsId("j118");
      else if (pvn == 759) biomeIdx = codec::plainsId("j119_759");
      else if (pvn == 760) biomeIdx = codec::plainsId("j119_760");
      else if (pvn <= 762) biomeIdx = codec::plainsId("j119_7612");
      else if (pvn == 763) biomeIdx = codec::plainsId("j120_763");
      else if (pvn <= 765) biomeIdx = codec::plainsId("c764");
      else if (pvn == 766) biomeIdx = codec::plainsId("e766");
      else if (pvn == 767) biomeIdx = codec::plainsId("e767");
      else if (pvn <= 769) biomeIdx = codec::plainsId("e769");
      else if (pvn <= 772) biomeIdx = codec::plainsId("e770");
      else if (pvn == 773) biomeIdx = codec::plainsId("e773");
      else if (pvn == 774) biomeIdx = codec::plainsId("e774");
      else biomeIdx = codec::plainsId("e775");
      for (int si = 0; si < 24; ++si) {
        if (!overlay) {
          emptySection(sections, biomeIdx);
          continue;
        }
        // World Y of section base: overworld minY -64.
        int sectionBaseY = -64 + si * 16;
        uint64_t bits[64] = {};
        bool any = false;
        const auto& s = *ctx.schem;
        for (int ly = 0; ly < 16; ++ly) {
          int wy = sectionBaseY + ly;
          int sy = wy - ctx.baseY;
          if (sy < 0 || sy >= s.h) continue;
          for (int lxx = 0; lxx < 16; ++lxx) {  // section-local X
            int wx = chunkX * 16 + lxx;
            int sx = wx - ctx.baseX;
            if (sx < 0 || sx >= s.w) continue;
            for (int lzz = 0; lzz < 16; ++lzz) {  // section-local Z
              int wz = chunkZ * 16 + lzz;
              int sz = wz - ctx.baseZ;
              if (sz < 0 || sz >= s.l) continue;
              int idx = (sy * s.l + sz) * s.w + sx;
              if (!schemSolid(s, idx)) continue;
              int cell = (ly * 16 + lzz) * 16 + lxx;  // vanilla (y,z,x) order
              bits[cell / 64] |= 1ULL << (cell % 64);
              any = true;
            }
          }
        }
        pasteSection(sections, biomeIdx, bits, any);
      }
      w.varInt(static_cast<int32_t>(sections.b.size()));
      w.bytes(sections.b);
      w.varInt(0);  // block entities
      if (pvn >= 757 && pvn <= 762) w.boolean(true);  // trustEdges (1.18-1.19.x)
      // Sky light full-bright (26 sections = 24 + 2 edge); block light
      // empty (correct darkness for blocklight).
      w.varInt(1);
      w.i64(0x3FFFFFF);  // skyLightMask: all sections carry data
      w.varInt(0);       // blockLightMask: none
      w.varInt(0);       // emptySkyLightMask: none
      w.varInt(1);
      w.i64(0x3FFFFFF);  // emptyBlockLightMask: all
      writeFullBrightSky(w);
      w.varInt(0);  // blockLight arrays
      break;
    }
  }
  return w.b;
}

// Standalone Update Light (1.17/1.17.1 only, packet 0x25): chunkX, chunkZ
// (VarInt), trustEdges, then varint-counted i64 sky/block/emptySky/emptyBlock
// masks and varint-counted light arrays (protocol_1.17.json
// packet_update_light). Full-bright sky via real 0xFF arrays (empty masks
// mean zero light). Empty for every other PVN.
std::vector<uint8_t> buildUpdateLightBody(int pvn, int chunkX, int chunkZ) {
  if (pvn != 755 && pvn != 756) return {};
  proto::Writer w;
  w.varInt(chunkX);
  w.varInt(chunkZ);
  w.boolean(false);  // trustEdges
  w.varInt(1);
  w.i64(0x3FFFFFF);  // skyLightMask: all 26 sections carry data
  w.varInt(0);       // blockLightMask: none
  w.varInt(0);       // emptySkyLightMask: none
  w.varInt(1);
  w.i64(0x3FFFFFF);  // emptyBlockLightMask: all
  writeFullBrightSky(w);
  w.varInt(0);       // blockLight arrays
  return w.b;
}

}  // namespace limbo::world
