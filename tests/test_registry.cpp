// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "protocol/registry.h"
#include "protocol/buffer.h"

using Bytes = std::vector<uint8_t>;

static bool contains(const Bytes& hay, const std::string& needle) {
  if (needle.empty() || hay.size() < needle.size()) return false;
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    bool ok = true;
    for (size_t j = 0; j < needle.size(); ++j)
      if (hay[i + j] != static_cast<uint8_t>(needle[j])) { ok = false; break; }
    if (ok) return true;
  }
  return false;
}

// Locate the RegistryData body whose leading bytes are VarInt(len)+regId.
// Vanilla order puts worldgen/biome first, so dimension_type is found, not [0].
static const Bytes* findRegBody(const std::vector<Bytes>& blobs,
                                const std::string& regId) {
  for (const auto& b : blobs)
    if (!b.empty() && b[0] == static_cast<uint8_t>(regId.size()) &&
        contains(b, regId))
      return &b;
  return nullptr;
}

int main() {
  using namespace limbo::registry;

  // --- configIds spot checks: 764/765 vs 766/767 (minecraft-data verified) ---
  ConfigIds c764 = configIds(764);
  assert(c764.registryData == 0x05 && c764.finish == 0x02 &&
         c764.keepAlive == 0x03 && c764.disconnect == 0x01);
  ConfigIds c765 = configIds(765);
  assert(c765.registryData == 0x05 && c765.finish == 0x02 &&
         c765.keepAlive == 0x03 && c765.disconnect == 0x01);
  ConfigIds c766 = configIds(766);
  assert(c766.registryData == 0x07 && c766.finish == 0x03 &&
         c766.keepAlive == 0x04 && c766.disconnect == 0x02);
  ConfigIds c767 = configIds(767);
  assert(c767.registryData == 0x07 && c767.finish == 0x03 &&
         c767.keepAlive == 0x04 && c767.disconnect == 0x02);
  // pre-Configuration eras reuse the 764 table (caller gates on hasConfiguration).
  assert(configIds(763).registryData == 0x05);

  // --- Tags / KnownPacks / Finish-ack ids ---
  assert(configTagsId(764) == 0x08);
  assert(configTagsId(765) == 0x09);
  assert(configTagsId(766) == 0x0D && configTagsId(767) == 0x0D);
  assert(configKnownPacksId(764) == 0 && configKnownPacksId(765) == 0);
  assert(configKnownPacksId(766) == 0x0E && configKnownPacksId(767) == 0x0E);
  assert(configKnownPacksServerboundId(766) == 0x07);
  assert(configKnownPacksServerboundId(764) == 0);
  assert(configFinishAckId(764) == 0x02 && configFinishAckId(766) == 0x03);

  // --- registry blobs: non-empty for every Config era, era-shaped ---
  for (int pvn : {764, 765, 766, 767}) {
    auto blobs = buildRegistryBlobs(pvn);
    assert(!blobs.empty());
    for (const auto& blob : blobs) assert(!blob.empty());
  }

  // 764/765: anonymous root compound (0x0A) holding the codec wrapper.
  for (int pvn : {764, 765}) {
    auto blobs = buildRegistryBlobs(pvn);
    assert(blobs.size() == 1);
    const Bytes& b = blobs[0];
    assert(b.front() == 0x0A);
    assert(b.back() == 0x00);  // root TAG_End
    assert(contains(b, "minecraft:dimension_type"));
    assert(contains(b, "minecraft:overworld"));
    assert(contains(b, "monster_spawn_light_level"));
  }

  // 766+: one body per registry, ALL of them, in vanilla order
  // (worldgen/biome is first in the source; dimension_type is located below).
  for (int pvn : {766, 767}) {
    auto blobs = buildRegistryBlobs(pvn);
    assert(blobs.size() >= 6);
    const std::string regId = "minecraft:dimension_type";
    const Bytes* found = findRegBody(blobs, regId);
    assert(found != nullptr);
    const Bytes& b = *found;
    assert(b[0] == static_cast<uint8_t>(regId.size()));  // 24 -> 0x18
    assert(contains(b, regId));
    assert(contains(b, "minecraft:overworld"));
    assert(b.back() == 0x00);  // element compound TAG_End
    // registry id is first: len byte + ASCII
    for (size_t j = 0; j < regId.size(); ++j)
      assert(b[1 + j] == static_cast<uint8_t>(regId[j]));
    // after id: VarInt entry count = 1, then key len byte.
    size_t after = 1 + regId.size();
    assert(b[after] == 0x01);  // count
    const std::string key = "minecraft:overworld";
    assert(b[after + 1] == static_cast<uint8_t>(key.size()));  // 19 -> 0x13
    assert(b[after + 2 + key.size()] == 0x01);                 // present flag
    assert(b[after + 3 + key.size()] == 0x0A);                 // anonymous NBT compound
  }

  // --- R/registry-complete (appended): every registry of the era is emitted ---
  const std::string kPaintingReg = "minecraft:painting_variant";
  const std::string kWolfReg = "minecraft:wolf_variant";
  // Every 766+ target sends the FULL era registry set (>=6 bodies, all
  // non-empty). wolf_variant exists from 1.20.5 (e766) on; painting_variant
  // from 1.21 (e767) on. pvn 776 (26.2, the client that rejected the old
  // 4-blob subset with `Registry must be non-empty`) maps to e775.
  for (int pvn : {766, 767, 769, 770, 773, 774, 775, 776}) {
    auto blobs = buildRegistryBlobs(pvn);
    assert(blobs.size() >= 6);
    for (const auto& b : blobs) assert(!b.empty());

    // Byte-search the generated bodies for the two ids the vanilla client
    // complained about, as proof they are on the wire.
    const Bytes* wolf = findRegBody(blobs, kWolfReg);
    assert(wolf != nullptr && !wolf->empty());
    assert(contains(*wolf, "minecraft:"));
    const Bytes* painting = findRegBody(blobs, kPaintingReg);
    if (pvn == 766) {
      assert(painting == nullptr);  // 1.20.5 has no painting_variant registry
    } else {
      assert(painting != nullptr && !painting->empty());
      assert(contains(*painting, "minecraft:"));
    }

    // dimension_type / biome bodies still present with baked vanilla ids.
    const Bytes* dim = findRegBody(blobs, "minecraft:dimension_type");
    assert(dim != nullptr && contains(*dim, "minecraft:overworld"));
    const Bytes* biome = findRegBody(blobs, "minecraft:worldgen/biome");
    assert(biome != nullptr && contains(*biome, "minecraft:plains"));
  }

  // 764/765 compound: the complete registry set that EXISTS in 1.20.2
  // (minecraft-data login_1.20.2.json dimensionCodec). painting_variant
  // (1.21) and wolf_variant (1.20.5) post-date that protocol version, so they
  // are absent from the source and are asserted absent here rather than
  // invented for a 1.20.2-era client; they are proven above for 766+.
  for (int pvn : {764, 765}) {
    auto blobs = buildRegistryBlobs(pvn);
    assert(blobs.size() == 1);
    const Bytes& b = blobs[0];
    for (const char* reg : {"minecraft:dimension_type", "minecraft:worldgen/biome",
                            "minecraft:chat_type", "minecraft:damage_type",
                            "minecraft:trim_pattern", "minecraft:trim_material"})
      assert(contains(b, reg));
    assert(!contains(b, kPaintingReg));
    assert(!contains(b, kWolfReg));
  }

  // --- Finish / KeepAlive bodies ---
  assert(buildFinish(764).empty());
  assert(buildFinish(767).empty());
  Bytes ka = buildKeepAlive(767, 12345);
  assert(ka == (Bytes{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x39}));
  assert(buildKeepAlive(764, -1) ==
         (Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));

  // --- Update Tags: real bindings, re-parseable (appended) ---
  // Wire shape per Via TagRewriter.handleGeneric/handle: VarInt registry
  // count; per registry STRING id + VarInt tag count; per tag STRING id +
  // VarInt member count + VarInt member ids.
  for (int pvn : {764, 765, 766, 767, 769, 770, 773, 774, 775, 776}) {
    Bytes t = buildTags(pvn);
    limbo::proto::Reader r(t);
    int32_t nRegs = r.varInt();
    assert(r.ok && nRegs > 0);
    bool sawOverworld = false, sawPause = false;
    int blockTags = 0;
    int32_t pauseMembers = -1;
    for (int32_t i = 0; i < nRegs; ++i) {
      std::string reg = r.str();
      assert(r.ok && reg.rfind("minecraft:", 0) == 0);
      int32_t nTags = r.varInt();
      assert(r.ok && nTags > 0);
      for (int32_t j = 0; j < nTags; ++j) {
        std::string id = r.str();
        assert(r.ok && id.rfind("minecraft:", 0) == 0);
        int32_t nMem = r.varInt();
        assert(r.ok && nMem >= 0);
        std::vector<int32_t> mem;
        for (int32_t k = 0; k < nMem; ++k) mem.push_back(r.varInt());
        assert(r.ok);
        if (reg == "minecraft:block") {
          ++blockTags;
          if (id == "minecraft:infiniburn_overworld") {
            sawOverworld = true;
            assert(mem.empty());  // block ids live in no synced blob (Via: empty)
          }
        }
        if (reg == "minecraft:dialog" && id == "minecraft:pause_screen_additions") {
          sawPause = true;
          pauseMembers = nMem;
          if (pvn >= 774) assert(nMem == 1 && mem[0] == 0);  // shipped custom_options
          else assert(nMem == 0);  // dialog registry not synced pre-774
        }
      }
    }
    assert(r.ok && r.remaining() == 0);
    if (pvn == 776) assert(blockTags == 6);  // trio + lightning/soul/wind
    else assert(blockTags >= 1);
    assert(sawOverworld && sawPause);
    if (pvn == 764) assert(!t.empty());
    (void)pauseMembers;
  }
  // buildTags(764): non-empty (block trio + dialog section).
  assert(!buildTags(764).empty());

  // --- 26.1+ REQUIRED tags (appended): damage_type (is_fire et al) +
  // banner_pattern must be bound or vanilla 26.x fails registry freeze with
  // "Missing tag". Exact lists mirror Via Protocol1_21_11To26_1. 774 keeps
  // the smaller table (native 1.21.11 never required them).
  {
    auto hasTag = [](const Bytes& t, const std::string& reg,
                     const std::string& id) {
      limbo::proto::Reader r(t);
      int32_t nRegs = r.varInt();
      for (int32_t i = 0; i < nRegs && r.ok; ++i) {
        std::string rr = r.str();
        int32_t nTags = r.varInt();
        for (int32_t j = 0; j < nTags && r.ok; ++j) {
          std::string ii = r.str();
          int32_t nMem = r.varInt();
          for (int32_t k = 0; k < nMem && r.ok; ++k) (void)r.varInt();
          if (r.ok && rr == reg && ii == id) return true;
        }
      }
      return false;
    };
    for (int pvn : {775, 776, 777}) {
      Bytes t = buildTags(pvn);
      assert(hasTag(t, "minecraft:damage_type", "minecraft:is_fire"));
      assert(hasTag(t, "minecraft:damage_type", "minecraft:mace_smash"));
      assert(hasTag(t, "minecraft:banner_pattern", "minecraft:no_item_required"));
      assert(hasTag(t, "minecraft:banner_pattern",
                    "minecraft:pattern_item/bordure_indented"));
    }
    Bytes t774 = buildTags(774);
    assert(!hasTag(t774, "minecraft:damage_type", "minecraft:is_fire"));
    assert(hasTag(t774, "minecraft:block", "minecraft:infiniburn_overworld"));

    // --- 775+ holder-complete tags (appended) ---
    // Every `#tag` holder in the shipped e776 blobs must be bound or 26.x
    // fails with "Missing tag". Enumerated by model-level scan, not memory.
    {
      auto hasTagIn = [&](const Bytes& t, const std::string& reg,
                          const std::string& id) {
        limbo::proto::Reader r(t);
        int32_t nRegs = r.varInt();
        for (int32_t i = 0; i < nRegs && r.ok; ++i) {
          std::string rr = r.str();
          int32_t nTags = r.varInt();
          for (int32_t j = 0; j < nTags && r.ok; ++j) {
            std::string ii = r.str();
            int32_t nMem = r.varInt();
            for (int32_t k = 0; k < nMem && r.ok; ++k) (void)r.varInt();
            if (r.ok && rr == reg && ii == id) return true;
          }
        }
        return false;
      };
      Bytes t776 = buildTags(776);
      // Enchantment holders.
      assert(hasTagIn(t776, "minecraft:item", "minecraft:enchantable/equippable"));
      assert(hasTagIn(t776, "minecraft:item", "minecraft:enchantable/armor"));
      assert(hasTagIn(t776, "minecraft:item", "minecraft:enchantable/weapon"));
      assert(hasTagIn(t776, "minecraft:entity_type",
                      "minecraft:sensitive_to_bane_of_arthropods"));
      assert(hasTagIn(t776, "minecraft:entity_type", "minecraft:arrows"));
      assert(hasTagIn(t776, "minecraft:enchantment", "minecraft:exclusive_set/damage"));
      assert(hasTagIn(t776, "minecraft:block", "minecraft:soul_speed_blocks"));
      assert(hasTagIn(t776, "minecraft:block", "minecraft:lightning_rods"));
      assert(hasTagIn(t776, "minecraft:block",
                      "minecraft:blocks_wind_charge_explosions"));
      assert(hasTagIn(t776, "minecraft:dialog", "minecraft:quick_actions"));
      // Full re-parse: zero trailing bytes.
      limbo::proto::Reader r(t776);
      int32_t nRegs = r.varInt();
      assert(r.ok && nRegs == 8);
      for (int32_t i = 0; i < nRegs; ++i) {
        (void)r.str();
        int32_t nTags = r.varInt();
        for (int32_t j = 0; j < nTags; ++j) {
          (void)r.str();
          int32_t nMem = r.varInt();
          for (int32_t k = 0; k < nMem; ++k) (void)r.varInt();
        }
      }
      assert(r.ok && r.remaining() == 0);
    }
  }

  // --- 26.2 jukebox_song `bounce` (appended): 776 only ---
  // Base is the FULL 26.1 member list (21 entries); 776 appends bounce (22).
  {
    auto blobs776 = buildRegistryBlobs(776);
    const Bytes* j776 = findRegBody(blobs776, "minecraft:jukebox_song");
    assert(j776 != nullptr);
    assert(contains(*j776, "minecraft:bounce"));
    assert(contains(*j776, "music_disc.bounce"));
    assert(contains(*j776, "jukebox_song.minecraft.bounce"));
    // Entry count went 21 -> 22, header still parses.
    limbo::proto::Reader r(*j776);
    std::string reg = r.str();
    int32_t count = r.varInt();
    assert(r.ok && reg == "minecraft:jukebox_song" && count == 22);
    auto blobs775 = buildRegistryBlobs(775);
    const Bytes* j775 = findRegBody(blobs775, "minecraft:jukebox_song");
    assert(j775 != nullptr && !contains(*j775, "bounce"));
    limbo::proto::Reader r775(*j775);
    assert(r775.str() == "minecraft:jukebox_song" && r775.varInt() == 21);
    // Base still carries a real 26.1 member (first-entry subsets are gone).
    assert(contains(*j775, "minecraft:11"));
  }

  // --- 26.2 element upgrades (appended): 776 carries 26.2 shapes ---
  // gen_codec.py to26_2 mirrors Via Protocol1_21_11To26_1 handlers.
  {
    auto blobs775 = buildRegistryBlobs(775);
    auto blobs776 = buildRegistryBlobs(776);
    const Bytes* c775 = findRegBody(blobs775, "minecraft:cat_variant");
    const Bytes* c776 = findRegBody(blobs776, "minecraft:cat_variant");
    assert(c775 != nullptr && c776 != nullptr);
    assert(!contains(*c775, "baby_asset_id"));
    assert(contains(*c776, "baby_asset_id"));
    assert(contains(*c776, "cat_all_black"));  // prefixed asset id
    const Bytes* t776 = findRegBody(blobs776, "minecraft:timeline");
    assert(t776 != nullptr && contains(*t776, "clock"));
    const Bytes* w776 = findRegBody(blobs776, "minecraft:wolf_variant");
    assert(w776 != nullptr && contains(*w776, "baby_assets"));
    // Same body count (upgrade never adds/removes registries/entries).
    assert(blobs775.size() == blobs776.size());
  }

  // --- 26.1+ sound-variant registries (appended): 775/776/777 ---
  // Upstream dropped them, but 26.x requires them non-empty. Bodies come
  // byte-faithful from Via's sound-variant-registries-26.1.nbt.
  for (int pvn : {775, 776}) {
    auto blobs = buildRegistryBlobs(pvn);
    for (const char* reg :
         {"minecraft:cat_sound_variant", "minecraft:cow_sound_variant",
          "minecraft:pig_sound_variant", "minecraft:chicken_sound_variant"}) {
      const Bytes* b = findRegBody(blobs, reg);
      assert(b != nullptr && !b->empty());
      limbo::proto::Reader r(*b);
      assert(r.ok && r.str() == reg && r.varInt() >= 2);
    }
    const Bytes* cat = findRegBody(blobs, "minecraft:cat_sound_variant");
    assert(cat != nullptr && contains(*cat, "adult_sounds"));
    const Bytes* cow = findRegBody(blobs, "minecraft:cow_sound_variant");
    assert(cow != nullptr && contains(*cow, "entity.cow.step"));
    // Unknown to older eras: absent there.
    auto b767 = buildRegistryBlobs(767);
    assert(findRegBody(b767, "minecraft:cat_sound_variant") == nullptr);
  }

  // --- 26.1+ world_clock registry (appended): 775/776/777 only ---
  {
    Bytes wc = buildWorldClock();
    limbo::proto::Reader r(wc);
    assert(r.ok && r.str() == "world_clock" && r.varInt() == 2);
    for (const char* want : {"minecraft:overworld", "minecraft:the_end"}) {
      assert(r.str() == want);
      assert(r.ok && r.u8() == 1);     // present
      assert(r.ok && r.u8() == 0x0A);  // anonymous compound type byte
      assert(r.ok && r.u8() == 0x00);  // empty element (TAG_End)
    }
    assert(r.ok && r.remaining() == 0);
    for (int pvn : {775, 776, 777}) {
      auto blobs = buildRegistryBlobs(pvn);
      const Bytes* found = findRegBody(blobs, "world_clock");
      assert(found != nullptr && *found == wc);
    }
    // Older eras never knew world_clock: absent there.
    {
      auto b774 = buildRegistryBlobs(774);
      auto b767 = buildRegistryBlobs(767);
      assert(findRegBody(b774, "world_clock") == nullptr);
      assert(findRegBody(b767, "world_clock") == nullptr);
    }
  }

  printf("test_registry ok\n");
  return 0;
}
