// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "world/void_chunk.h"

#include <mutex>
#include <optional>
#include <string>

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
void emptySection(limbo::proto::Writer& w, int biomeIdx) {
  w.i16(0);      // block count
  w.u8(0);       // blocks BPE: single-valued
  w.varInt(1);   // palette length
  w.varInt(0);   // air (global id 0, stable since 1.13 flattening)
  w.varInt(0);   // data array length
  w.u8(0);       // biomes BPE: single-valued
  w.varInt(1);
  w.varInt(biomeIdx);
  w.varInt(0);
}

// 1.18+ section with schematic overlay: BPE=1 palette [air, stone(global 1)],
// 64 longs bit-packed, cell index (y*16+z)*16+x. Solid = non-air palette name.
void pasteSection(limbo::proto::Writer& w, int biomeIdx, const uint64_t bits[64],
                  bool anySolid) {
  int blocks = 0;
  for (int i = 0; i < 64; ++i) blocks += __builtin_popcountll(bits[i]);
  if (!anySolid || blocks == 0) {
    emptySection(w, biomeIdx);
    return;
  }
  w.i16(static_cast<int16_t>(blocks));
  w.u8(1);  // BPE 1
  w.varInt(2);
  w.varInt(0);  // air
  w.varInt(1);  // stone (global id 1, stable since 1.13 flattening)
  w.varInt(64);
  for (int i = 0; i < 64; ++i) w.i64(static_cast<int64_t>(bits[i]));
  w.u8(0);
  w.varInt(1);
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

}  // namespace

namespace limbo::world {

namespace {
std::optional<schem::Schem> g_spawn;
std::once_flag g_spawn_flag;
std::string g_spawn_path;
}  // namespace

void setSpawnSchematic(const std::string& path) { g_spawn_path = path; }

bool loadSpawnSchematic(std::string& err) {
  if (g_spawn_path.empty()) return true;  // void-only, not an error
  std::call_once(g_spawn_flag, [] {
    schem::Error e;
    auto s = schem::loadSchem(g_spawn_path, e);
    if (s) {
      g_spawn = std::move(*s);
    } else {
      g_spawn.reset();
      // stash error via static (call_once runs once; report through param below)
      static std::string errOnce = e;
      (void)errOnce;
    }
  });
  if (!g_spawn) {
    err = "cannot load schematic, falling back to void";
    return false;
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

double spawnY() {
  if (!g_spawn) return 400.0;  // void: safely above everything
  return (double)(320 - g_spawn->h) + g_spawn->h;  // feet exactly on platform top
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
    case proto::Era::V1_13:
      // Chunk Data: ground-up, varint bitmap, data, entities.
      w.boolean(true);
      w.varInt(0);
      w.varInt(0);
      w.varInt(0);
      break;
    case proto::Era::V1_14:
      w.boolean(true);
      w.varInt(0);
      w.bytes(emptyNbt());
      w.varInt(0);
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
        w.varInt(0);  // biomes: none
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
      if (pvn <= 754) biomeIdx = 1;  // 1.16-1.18 plains (verified in codec data)
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
          for (int lz = 0; lz < 16; ++lz) {
            int wx = chunkX * 16 + lz;
            int sx = wx - ctx.baseX;
            if (sx < 0 || sx >= s.w) continue;
            for (int lx = 0; lx < 16; ++lx) {
              int wz = chunkZ * 16 + lx;
              int sz = wz - ctx.baseZ;
              if (sz < 0 || sz >= s.l) continue;
              int idx = (sy * s.l + sz) * s.w + sx;
              if (!schemSolid(s, idx)) continue;
              int cell = (ly * 16 + lz) * 16 + lx;
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
      w.varInt(0);  // skyLightMask: none
      w.varInt(0);  // blockLightMask: none
      // empty masks: 26 sections (24 + 2 edge) all empty => full bright.
      w.varInt(1);
      w.i64(0x3FFFFFF);
      w.varInt(1);
      w.i64(0x3FFFFFF);
      w.varInt(0);  // skyLight arrays
      w.varInt(0);  // blockLight arrays
      break;
    }
  }
  return w.b;
}

}  // namespace limbo::world
