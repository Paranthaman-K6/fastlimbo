// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include <cassert>
#include <cstdio>
#include <cstring>
#include "world/void_chunk.h"
#include "world/schematic.h"
#include "protocol/buffer.h"
#include "protocol/codec_data.h"

int main() {
  using namespace limbo::world;
  using namespace limbo::proto;

  // --- Test 1: non-empty body for modern + legacy PVN ---
  auto body = buildVoidChunkBody(763, 0, 0);
  assert(!body.empty());
  auto legacy = buildVoidChunkBody(47, 0, 0);
  assert(!legacy.empty());

  // --- Test 2: exact byte layout of one empty section for 763 ---
  // BPE=0 single-valued palette encoding (Via PaletteType1_18.write):
  //   BPE(0) + paletteID(VarInt) + dataLength(VarInt=0)  — NO palette length.
  // Empty section = i16(0) + [u8(0) + varInt(0) + varInt(0)] + [u8(0) + varInt(39) + varInt(0)]
  // plains biome idx for 763 = codec::plainsId("j120_763") = 39 = 0x27.
  assert(limbo::codec::plainsId("j120_763") == 39);
  const uint8_t expectedSection[] = {
    0x00, 0x00,  // i16 block count = 0
    0x00,        // blocks BPE = 0 (single-valued)
    0x00,        // block palette ID = 0 (air)
    0x00,        // block data length = 0
    0x00,        // biomes BPE = 0 (single-valued)
    0x27,        // biome palette ID = 39 (plains)
    0x00,        // biome data length = 0
  };
  constexpr size_t kSectionLen = 8;

  // --- Test 3: chunk body parses — x, z + 24 sections + light consume exactly ---
  Reader r(body);
  int32_t x = r.i32();
  int32_t z = r.i32();
  assert(x == 0 && z == 0);
  assert(r.ok);

  // Skip heightmaps NBT: TAG_Compound(10) [+ u16(0) name] ... TAG_End(0).
  // For 763 (< 764) named root: u8(10) + u16(0) + ... + u8(0).
  assert(r.u8() == 10);  // TAG_Compound
  assert(r.u16() == 0);  // empty root name
  // Walk to matching TAG_End: nested compound has one LongArray + End.
  assert(r.u8() == 12);  // TAG_Long_Array
  assert(r.u16() == 15); // name length
  auto nameBytes = r.bytes(15);
  assert(r.ok && nameBytes.size() == 15);
  assert(r.i32() == 37); // long count
  auto longs = r.bytes(37 * 8);
  assert(r.ok && longs.size() == 37 * 8);
  assert(r.u8() == 0);   // TAG_End (root)
  assert(r.ok);

  // Sections buffer: varInt length + 24 × kSectionLen bytes.
  int32_t sectionsLen = r.varInt();
  assert(r.ok && sectionsLen == 24 * (int32_t)kSectionLen);
  // Every section must match the expected empty layout exactly.
  for (int i = 0; i < 24; ++i) {
    auto sec = r.bytes(kSectionLen);
    assert(r.ok && sec.size() == kSectionLen);
    assert(std::memcmp(sec.data(), expectedSection, kSectionLen) == 0);
  }
  // Block entities count.
  assert(r.varInt() == 0);
  assert(r.ok);
  // No trustEdges for 763 (only 757-762).
  // Light: full-bright sky arrays (empty masks mean zero light per wiki).
  assert(r.varInt() == 1);        // skyLightMask: count 1
  assert(r.i64() == 0x3FFFFFF);   // all 26 sections carry data
  assert(r.varInt() == 0);        // blockLightMask: count 0
  assert(r.varInt() == 0);        // emptySkyLightMask: count 0
  assert(r.varInt() == 1);        // emptyBlockLightMask: count 1
  assert(r.i64() == 0x3FFFFFF);
  assert(r.varInt() == 26);       // skyLight arrays: count 26
  for (int i = 0; i < 26; ++i) {
    auto arr = r.bytes(2048);
    assert(r.ok && arr.size() == 2048 && arr[0] == 0xFF && arr[2047] == 0xFF);
  }
  assert(r.varInt() == 0);        // blockLight arrays: count 0
  assert(r.ok);
  assert(r.remaining() == 0);     // no trailing bytes

  // --- Test 4: paste section byte layout (BPE=4, 256 longs, 4-bit packing) ---
  {
    limbo::schem::Schem s;
    s.w = 16; s.h = 16; s.l = 16;
    s.palette = {"minecraft:air", "minecraft:stone"};
    s.blocks.assign(16 * 16 * 16, 0);  // all air
    // Stone at schem local (x=1, y=0, z=1) -> idx (0*16+1)*16+1 = 17.
    s.blocks[(0 * s.l + 1) * s.w + 1] = 1;
    limbo::world::PasteCtx ctx;
    ctx.schem = &s;
    ctx.baseX = 0; ctx.baseY = -64; ctx.baseZ = 0;  // covers chunk (0,0) section 0
    auto pbody = buildChunkBody(763, 0, 0, ctx);
    assert(!pbody.empty());

    Reader pr(pbody);
    assert(pr.i32() == 0 && pr.i32() == 0);  // x, z
    // heightmaps (named root for 763)
    assert(pr.u8() == 10);
    assert(pr.u16() == 0);
    assert(pr.u8() == 12);
    assert(pr.u16() == 15);
    pr.bytes(15);
    assert(pr.i32() == 37);
    pr.bytes(37 * 8);
    assert(pr.u8() == 0);
    assert(pr.ok);

    // Sections buffer.
    int32_t plen = pr.varInt();
    assert(pr.ok && plen > 24 * (int32_t)kSectionLen);  // paste section is larger
    // Section 0: paste (BPE=4, palette [air, stone], 256 longs).
    assert(pr.i16() == 1);      // block count = 1
    assert(pr.u8() == 4);       // BPE 4
    assert(pr.varInt() == 2);   // palette length
    assert(pr.varInt() == 0);   // air
    assert(pr.varInt() == 1);   // stone
    assert(pr.varInt() == 256); // long count
    // Stone cell: schem (x=1,z=1) -> section local (lz=1, lx=1), ly=0
    // -> cell (0*16+1)*16+1 = 17 -> long 1, nibble (17%16)*4 = 4.
    for (int i = 0; i < 256; ++i) {
      int64_t lv = pr.i64();
      if (i == 1) assert(lv == (1LL << 4));
      else        assert(lv == 0);
    }
    assert(pr.ok);
    // Biomes for section 0.
    assert(pr.u8() == 0);
    assert(pr.varInt() == 39);
    assert(pr.varInt() == 0);
    // Sections 1-23: empty.
    for (int i = 1; i < 24; ++i) {
      auto sec = pr.bytes(kSectionLen);
      assert(pr.ok && sec.size() == kSectionLen);
      assert(std::memcmp(sec.data(), expectedSection, kSectionLen) == 0);
    }
    // Block entities + light masks (full-bright sky arrays).
    assert(pr.varInt() == 0);
    assert(pr.varInt() == 1);
    assert(pr.i64() == 0x3FFFFFF);
    assert(pr.varInt() == 0);
    assert(pr.varInt() == 0);
    assert(pr.varInt() == 1);
    assert(pr.i64() == 0x3FFFFFF);
    assert(pr.varInt() == 26);
    for (int i = 0; i < 26; ++i) {
      auto arr = pr.bytes(2048);
      assert(pr.ok && arr.size() == 2048 && arr[0] == 0xFF);
    }
    assert(pr.varInt() == 0);
    assert(pr.ok && pr.remaining() == 0);
  }

  // --- Test 5: 1.17 (755) chunk body parses exactly (no light fields) ---
  // Shape per protocol_1.17.json packet_map_chunk: x, z, bitMap (i64 array),
  // heightmaps nbt, biomes (varint array), chunkData, blockEntities.
  {
    auto c755 = buildVoidChunkBody(755, 0, 0);
    assert(!c755.empty());
    Reader r(c755);
    assert(r.i32() == 0 && r.i32() == 0);  // x, z
    assert(r.varInt() == 0);               // bitMap: no longs (all air)
    assert(r.u8() == 10);                  // heightmaps TAG_Compound
    assert(r.u16() == 0);                  // empty root name
    assert(r.u8() == 12);                  // TAG_Long_Array
    assert(r.u16() == 15);                 // "MOTION_BLOCKING"
    r.bytes(15);
    assert(r.i32() == 37);
    r.bytes(37 * 8);
    assert(r.u8() == 0);                   // TAG_End
    // Full biome array: 16 sections x 64 (vanilla 1.17 height 256).
    // Empty breaks Via's 1_17_1->1_18 converter (indexes biomeData[i*64])
    // and native clients alike.
    assert(r.varInt() == 1024);          // biome count
    for (int i = 0; i < 1024; ++i)
      assert(r.varInt() == limbo::codec::plainsId("j117"));  // plains
    assert(r.varInt() == 0);               // chunk data: none
    assert(r.varInt() == 0);               // block entities: none
    assert(r.ok && r.remaining() == 0);
    // 756 identical (same j117 codec).
    {
      auto c756 = buildVoidChunkBody(756, 0, 0);
      assert(c756 == c755);
    }
  }

  // --- Test 6: 1.17 Update Light body parses exactly (full-bright) ---
  // Shape per protocol_1.17.json packet_update_light (id 0x25): chunkX,
  // chunkZ, trustEdges, 4x varint-counted i64 masks, 2x varint-counted light
  // arrays. Real 0xFF sky arrays (empty masks mean zero light per wiki).
  {
    auto l = buildUpdateLightBody(755, 0, 0);
    assert(!l.empty());
    Reader r(l);
    assert(r.varInt() == 0);      // chunkX
    assert(r.varInt() == 0);      // chunkZ
    assert(r.boolean() == false);  // trustEdges
    assert(r.varInt() == 1);      // skyLightMask: count 1
    assert(r.i64() == 0x3FFFFFF);  // all 26 sections carry data
    assert(r.varInt() == 0);      // blockLightMask: count 0
    assert(r.varInt() == 0);      // emptySkyLightMask: count 0
    assert(r.varInt() == 1);      // emptyBlockLightMask: count 1
    assert(r.i64() == 0x3FFFFFF);
    assert(r.varInt() == 26);     // skyLight arrays: count 26
    for (int i = 0; i < 26; ++i) {
      auto arr = r.bytes(2048);
      assert(r.ok && arr.size() == 2048 && arr[0] == 0xFF);
    }
    assert(r.varInt() == 0);      // blockLight arrays
    assert(r.ok && r.remaining() == 0);
    // 756 identical; other eras have no standalone light packet.
    assert(buildUpdateLightBody(756, 0, 0) == l);
    assert(buildUpdateLightBody(757, 0, 0).empty());
    assert(buildUpdateLightBody(763, 0, 0).empty());
  }

  // --- Test 7: schematic analysis report (tests/data/spawn.schem) ---
  // 8x3x8 stone floor (64) + 4 glass pillars = 68 solid, 124 air.
  {
    setSpawnSchematic("tests/data/spawn.schem");
    std::string err;
    assert(loadSpawnSchematic(err) && err.empty());
    auto rep = describeSpawnSchematic();
    auto has = [&](const char* sub) {
      for (auto& l : rep)
        if (l.find(sub) != std::string::npos) return true;
      return false;
    };
    assert(has("dims=8x3x8"));
    assert(has("solid=68"));
    assert(has("minecraft:stone=64"));
    assert(has("bounds x[4,11]"));
    assert(has("feet=319.0 (on-platform)"));  // corner pillar top y=318
    assert(has("x[0,0] z[0,0] (1 total, all in login burst)"));
    assert(has("pvn>=757"));
    assert(!rep.empty());
  }

  printf("test_void_chunk ok\n");
  return 0;
}
