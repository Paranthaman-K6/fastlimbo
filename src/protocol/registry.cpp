// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "protocol/registry.h"

#include <cstring>
#include <string>
#include <vector>

#include "protocol/buffer.h"
#include "protocol/codec_data.h"
#include "protocol/nbt.h"

namespace limbo::registry {

// Clientbound Configuration ids verified against minecraft-data protocol.json
// (1.20.2/1.20.3/1.20.5/1.21.1/1.21.4 + NanoLimbo State.java cross-check;
// 26.3 IDs from minecraft.wiki protocol 777: RegistryData 0x07, Finish 0x03,
// Update Tags 0x0E, Known Packs 0x0F — the 26.2->26.3 +1 shift moved Tags and
// KnownPacks but NOT RegistryData/Finish).
ConfigIds configIds(int pvn) {
  if (pvn >= 766) return {0x07, 0x03, 0x04, 0x02};
  return {0x05, 0x02, 0x03, 0x01};
}

int configTagsId(int pvn) {
  if (pvn >= 777) return 0x0E;  // 26.3 shifted +1
  if (pvn >= 766) return 0x0D;
  if (pvn >= 765) return 0x09;  // 1.20.3+ inserted remove/add resource pack
  if (pvn >= 764) return 0x08;
  return 0;
}

int configKnownPacksId(int pvn) {
  if (pvn >= 777) return 0x0F;  // 26.3 shifted +1
  return pvn >= 766 ? 0x0E : 0;
}
int configKnownPacksServerboundId(int pvn) { return pvn >= 766 ? 0x07 : 0; }

int configFinishAckId(int pvn) { return pvn >= 766 ? 0x03 : 0x02; }

namespace {

// Copy a generated blob into a byte vector.
std::vector<uint8_t> blobBytes(codec::Blob b) {
  return std::vector<uint8_t>(b.data, b.data + b.size);
}

// 26.2 adds the jukebox_song entry `bounce` (Via
// Protocol26_1To26_2.registerPackets:
// registryDataRewriter.addEntries("jukebox_song",
// Protocol1_21_11To26_1.createJukeboxPlayableEntry("bounce"))).
// Entry NBT mirrors that helper exactly: description{translate
// "jukebox_song.minecraft.bounce"}, sound_event "music_disc.bounce",
// length_in_seconds 175.0f, comparator_output 10. The key is qualified
// ("minecraft:bounce") like every shipped entry ("minecraft:11", ...).
std::vector<uint8_t> withBounce(const std::vector<uint8_t>& body) {
  // RegistryData body: String registry id + VarInt count + entries
  // (String key, Boolean present, anonymous NBT element). Re-emit the
  // header with count+1 and append the new entry; the old tail is opaque.
  proto::Reader r(body);
  std::string reg = r.str();
  int32_t count = r.varInt();
  if (!r.ok) return body;
  nbt::NbtWriter n;
  n.beginRootCompound();
  n.beginCompound("description");
  n.stringTag("translate", "jukebox_song.minecraft.bounce");
  n.endCompound();
  n.stringTag("sound_event", "music_disc.bounce");
  n.floatTag("length_in_seconds", 175.0f);
  n.intTag("comparator_output", 10);
  n.endCompound();
  proto::Writer w;
  w.str(reg);
  w.varInt(count + 1);
  w.bytes(body.data() + r.pos, body.size() - r.pos);
  w.str("minecraft:bounce");
  w.u8(1);  // present
  w.bytes(n.b);
  return w.b;
}

// Update Tags section tables (see buildTags below).
struct TagDef {
  const char* id;
  const int* members;  // ids into OUR shipped blobs; nullptr == empty
  size_t nMembers;
};
struct TagSection {
  const char* registry;
  const TagDef* tags;
  size_t nTags;
};
// Index 0 of our shipped single-entry blobs (dialog custom_options,
// timeline day); valid by construction (the registry is non-empty).
const int kFirstShipped[] = {0};
const TagDef kBlockTags[] = {
    {"minecraft:infiniburn_nether", nullptr, 0},
    {"minecraft:infiniburn_end", nullptr, 0},
    {"minecraft:infiniburn_overworld", nullptr, 0},
};
const TagDef kItemTags[] = {
    {"minecraft:enchantable/head_armor", nullptr, 0},
};
// 26.1+ item/block tags referenced by full-list enchantment elements
// (supported_items/primary_items/predicate holders). All empty: limbo has
// no enchanting, so no members are needed — but the names must be bound or
// vanilla 26.x fails with "Missing tag". Enumerated by NBT-aware scan of
// the shipped e776 blobs (every `#namespace:id` holder), not from memory.
const TagDef kItemTags26[] = {
    {"minecraft:enchantable/armor", nullptr, 0},
    {"minecraft:enchantable/bow", nullptr, 0},
    {"minecraft:enchantable/chest_armor", nullptr, 0},
    {"minecraft:enchantable/crossbow", nullptr, 0},
    {"minecraft:enchantable/durability", nullptr, 0},
    {"minecraft:enchantable/equippable", nullptr, 0},
    {"minecraft:enchantable/fire_aspect", nullptr, 0},
    {"minecraft:enchantable/fishing", nullptr, 0},
    {"minecraft:enchantable/foot_armor", nullptr, 0},
    {"minecraft:enchantable/head_armor", nullptr, 0},
    {"minecraft:enchantable/leg_armor", nullptr, 0},
    {"minecraft:enchantable/lunge", nullptr, 0},
    {"minecraft:enchantable/mace", nullptr, 0},
    {"minecraft:enchantable/melee_weapon", nullptr, 0},
    {"minecraft:enchantable/mining", nullptr, 0},
    {"minecraft:enchantable/mining_loot", nullptr, 0},
    {"minecraft:enchantable/sharp_weapon", nullptr, 0},
    {"minecraft:enchantable/sweeping", nullptr, 0},
    {"minecraft:enchantable/trident", nullptr, 0},
    {"minecraft:enchantable/vanishing", nullptr, 0},
    {"minecraft:enchantable/weapon", nullptr, 0},
};
const TagDef kBlockTags26[] = {
    {"minecraft:infiniburn_nether", nullptr, 0},
    {"minecraft:infiniburn_end", nullptr, 0},
    {"minecraft:infiniburn_overworld", nullptr, 0},
    {"minecraft:lightning_rods", nullptr, 0},
    {"minecraft:soul_speed_blocks", nullptr, 0},
    {"minecraft:blocks_wind_charge_explosions", nullptr, 0},
};
const TagDef kEntityTypeTags[] = {
    {"minecraft:arrows", nullptr, 0},
    {"minecraft:sensitive_to_bane_of_arthropods", nullptr, 0},
    {"minecraft:sensitive_to_impaling", nullptr, 0},
    {"minecraft:sensitive_to_smite", nullptr, 0},
};
const TagDef kEnchantmentTags[] = {
    {"minecraft:exclusive_set/armor", nullptr, 0},
    {"minecraft:exclusive_set/boots", nullptr, 0},
    {"minecraft:exclusive_set/bow", nullptr, 0},
    {"minecraft:exclusive_set/crossbow", nullptr, 0},
    {"minecraft:exclusive_set/damage", nullptr, 0},
    {"minecraft:exclusive_set/mining", nullptr, 0},
    {"minecraft:exclusive_set/riptide", nullptr, 0},
};
const TagDef kDialogTagsBound[] = {
    {"minecraft:pause_screen_additions", kFirstShipped, 1},
    {"minecraft:quick_actions", nullptr, 0},
};
// 774 keeps pause-only (quick_actions tag binding is 26.x-era).
const TagDef kDialogTags774[] = {
    {"minecraft:pause_screen_additions", kFirstShipped, 1},
};
const TagDef kDialogTagsEmpty[] = {
    {"minecraft:pause_screen_additions", nullptr, 0},
};
const TagDef kTimelineTags[] = {
    {"minecraft:in_overworld", kFirstShipped, 1},
};
// 764/765 (c764/c765) and 766 (e766): only infiniburn is referenced; the
// dialog registry is not synced yet, so its tag binds empty.
const TagSection kTagsBase[] = {
    {"minecraft:block", kBlockTags, 3},
    {"minecraft:dialog", kDialogTagsEmpty, 1},
};
// 767..773 (e767/e769/e770/e773): + enchantment supported_items ref.
const TagSection kTags767[] = {
    {"minecraft:block", kBlockTags, 3},
    {"minecraft:item", kItemTags, 1},
    {"minecraft:dialog", kDialogTagsEmpty, 1},
};
// 26.1+ REQUIRED tags (vanilla freeze-validates them even when no shipped
// element references them; "Missing tag" otherwise). Exact lists from
// ViaVersion Protocol1_21_11To26_1.onMappingDataLoaded, which backfills these
// empty for older servers. All empty (no member ids) exactly like Via.
const TagDef kDamageTags[] = {
    {"minecraft:damages_helmet", nullptr, 0},
    {"minecraft:bypasses_armor", nullptr, 0},
    {"minecraft:bypasses_shield", nullptr, 0},
    {"minecraft:bypasses_invulnerability", nullptr, 0},
    {"minecraft:bypasses_cooldown", nullptr, 0},
    {"minecraft:bypasses_effects", nullptr, 0},
    {"minecraft:bypasses_resistance", nullptr, 0},
    {"minecraft:bypasses_enchantments", nullptr, 0},
    {"minecraft:is_fire", nullptr, 0},
    {"minecraft:is_projectile", nullptr, 0},
    {"minecraft:witch_resistant_to", nullptr, 0},
    {"minecraft:is_explosion", nullptr, 0},
    {"minecraft:is_fall", nullptr, 0},
    {"minecraft:is_drowning", nullptr, 0},
    {"minecraft:is_freezing", nullptr, 0},
    {"minecraft:is_lightning", nullptr, 0},
    {"minecraft:no_anger", nullptr, 0},
    {"minecraft:no_impact", nullptr, 0},
    {"minecraft:always_most_significant_fall", nullptr, 0},
    {"minecraft:wither_immune_to", nullptr, 0},
    {"minecraft:ignites_armor_stands", nullptr, 0},
    {"minecraft:burns_armor_stands", nullptr, 0},
    {"minecraft:avoids_guardian_thorns", nullptr, 0},
    {"minecraft:always_triggers_silverfish", nullptr, 0},
    {"minecraft:always_hurts_ender_dragons", nullptr, 0},
    {"minecraft:no_knockback", nullptr, 0},
    {"minecraft:always_kills_armor_stands", nullptr, 0},
    {"minecraft:can_break_armor_stand", nullptr, 0},
    {"minecraft:bypasses_wolf_armor", nullptr, 0},
    {"minecraft:is_player_attack", nullptr, 0},
    {"minecraft:burn_from_stepping", nullptr, 0},
    {"minecraft:panic_causes", nullptr, 0},
    {"minecraft:panic_environmental_causes", nullptr, 0},
    {"minecraft:mace_smash", nullptr, 0},
};
const TagDef kBannerTags[] = {
    {"minecraft:no_item_required", nullptr, 0},
    {"minecraft:pattern_item/flower", nullptr, 0},
    {"minecraft:pattern_item/creeper", nullptr, 0},
    {"minecraft:pattern_item/skull", nullptr, 0},
    {"minecraft:pattern_item/mojang", nullptr, 0},
    {"minecraft:pattern_item/globe", nullptr, 0},
    {"minecraft:pattern_item/piglin", nullptr, 0},
    {"minecraft:pattern_item/flow", nullptr, 0},
    {"minecraft:pattern_item/guster", nullptr, 0},
    {"minecraft:pattern_item/field_masoned", nullptr, 0},
    {"minecraft:pattern_item/bordure_indented", nullptr, 0},
};
// 774+ (e774/e775): + dialog + timeline registries (members shipped).
const TagSection kTags774[] = {
    {"minecraft:block", kBlockTags, 3},
    {"minecraft:item", kItemTags, 1},
    {"minecraft:dialog", kDialogTags774, 1},
    {"minecraft:timeline", kTimelineTags, 1},
};
// 775+ (26.1, 26.2, 26.3): kTags774 + REQUIRED damage/banner tags above
// + the full enchantment holder set (item/block/entity_type/enchantment
// sections). Scoped to 775+: native 1.21.11 (774) never required them, and
// unknown tag sections could trip older clients, so 774 keeps the smaller
// table. Counts: block 6, item 21, dialog 2, timeline 1, damage 34,
// banner 11, entity_type 4, enchantment 7.
const TagSection kTags775[] = {
    {"minecraft:block", kBlockTags26, 6},
    {"minecraft:item", kItemTags26, 21},
    {"minecraft:dialog", kDialogTagsBound, 2},
    {"minecraft:timeline", kTimelineTags, 1},
    {"minecraft:damage_type", kDamageTags, 34},
    {"minecraft:banner_pattern", kBannerTags, 11},
    {"minecraft:entity_type", kEntityTypeTags, 4},
    {"minecraft:enchantment", kEnchantmentTags, 7},
};

// Era mirror of buildRegistryBlobs' target selection above.
const TagSection* tagsForPvn(int pvn, size_t* n) {
  if (pvn >= 775) {  // e775 (775/776/777+)
    *n = sizeof(kTags775) / sizeof(kTags775[0]);
    return kTags775;
  }
  if (pvn == 774) {  // e774 (774)
    *n = sizeof(kTags774) / sizeof(kTags774[0]);
    return kTags774;
  }
  if (pvn == 764 || pvn == 765 || pvn == 766) {
    *n = sizeof(kTagsBase) / sizeof(kTagsBase[0]);
    return kTagsBase;
  }
  *n = sizeof(kTags767) / sizeof(kTags767[0]);
  return kTags767;  // 767..773
}

}  // namespace

std::vector<std::vector<uint8_t>> buildRegistryBlobs(int pvn) {
  // Real registry data from minecraft-data loginPacket.json. Every registry
  // present in the source is emitted (R/registry-complete): a vanilla client
  // rejects a filtered subset with `Registry must be non-empty`. Only
  // dimension_type (overworld only) and worldgen/biome (plains only) stay
  // filtered, because the chunk biome palette and the JoinGame dimension
  // index depend on those baked vanilla ids. See tools/gen_codec.py for
  // provenance and the reuse map.
  std::vector<std::vector<uint8_t>> out;
  if (pvn == 764 || pvn == 765) {
    // Compound era: one anonymous-root compound holding every source registry.
    out.push_back(blobBytes(pvn == 764 ? codec::c764() : codec::c765()));
    return out;
  }
  const char* target = "e766";
  if (pvn == 766) target = "e766";
  else if (pvn == 767) target = "e767";
  else if (pvn == 768 || pvn == 769) target = "e769";
  else if (pvn >= 770 && pvn <= 772) target = "e770";
  else if (pvn == 773) target = "e773";
  else if (pvn == 774) target = "e774";
  else if (pvn == 775) target = "e775";  // verbatim 26.1 shapes
  else target = "e776";  // 776/777+: 26.2 element shapes (baby assets, ...)
  // One RegistryData body per registry, in vanilla (source) order.
  size_t n = 0;
  const codec::RegBlob* table = nullptr;
  if (!std::strcmp(target, "e766")) table = codec::e766_regs(&n);
  else if (!std::strcmp(target, "e767")) table = codec::e767_regs(&n);
  else if (!std::strcmp(target, "e769")) table = codec::e769_regs(&n);
  else if (!std::strcmp(target, "e770")) table = codec::e770_regs(&n);
  else if (!std::strcmp(target, "e773")) table = codec::e773_regs(&n);
  else if (!std::strcmp(target, "e774")) table = codec::e774_regs(&n);
  else if (!std::strcmp(target, "e775")) table = codec::e775_regs(&n);
  else table = codec::e776_regs(&n);
  for (size_t i = 0; i < n; ++i) {
    if (table[i].blob.size == 0) continue;  // registry absent in source data
    std::vector<uint8_t> body = blobBytes(table[i].blob);
    // 26.2 (pvn>=776) only: append the `bounce` jukebox_song entry.
    // 775 and below are byte-identical to the generated blobs.
    if (pvn >= 776 && std::strcmp(table[i].registry, "minecraft:jukebox_song") == 0)
      body = withBounce(body);
    out.push_back(std::move(body));
  }
  // 26.1+ only: the `world_clock` registry (overworld + the_end, empty
  // elements). Absent from minecraft-data's 1.21.11 snapshot, but Via's
  // Protocol1_21_11To26_1 sends exactly this at Finish, so 26.x requires it.
  // Older eras never knew the registry: not sent there.
  if (pvn >= 775) out.push_back(buildWorldClock());
  return out;
}

// 26.1+ `world_clock` RegistryData body, byte-mirroring Via
// Protocol1_21_11To26_1 (REGISTRY_DATA string "world_clock", two entries with
// empty anonymous compound elements, present=true).
std::vector<uint8_t> buildWorldClock() {
  proto::Writer w;
  w.str("world_clock");  // bare id, exactly as Via writes it
  w.varInt(2);
  for (const char* key : {"minecraft:overworld", "minecraft:the_end"}) {
    w.str(key);
    w.u8(1);  // present
    // Empty anonymous NBT compound: TAG_Compound type byte + TAG_End.
    // A bare 0x00 means NULL and vanilla rejects it with
    // "Expected non-null compound tag".
    w.u8(0x0A);
    w.u8(0x00);
  }
  return w.b;
}

std::vector<uint8_t> buildFinish(int pvn) {
  (void)pvn;  // Finish Configuration has no fields in any era.
  return {};
}

// Update Tags body (Configuration + Play share the shape).
// Wire format (Via TagRewriter,
// common/src/main/java/com/viaversion/viaversion/rewriter/TagRewriter.java,
// NOT from memory): handleGeneric/handle/appendNewTags write a VarInt
// registry count; per registry a STRING registry id + VarInt tag count; per
// tag a STRING tag id + VAR_INT_ARRAY_PRIMITIVE entries (VarInt length +
// one VarInt per member id). Types.STRING is a VarInt byte-length + UTF-8
// (Writer::str, buffer.cpp). Keys/ids are fully qualified
// ("minecraft:block") as vanilla sends them; vanilla normalizes bare ids,
// so Via's bare backfill ids ("infiniburn_nether") are equivalent.
// Sections bind the `#namespace:id` holders found by NBT-aware scan of the
// shipped blobs (codec_data.cpp; raw byte search false-positives on 0x23
// length bytes and hex colours excluded):
//   dimension_type.overworld.infiniburn   -> block infiniburn_overworld (*)
//   enchantment.*.supported_items         -> item enchantable/head_armor
//   dialog.custom_options.dialogs         -> dialog pause_screen_additions
//   dimension_type.overworld.timelines    -> timeline in_overworld
// (*) ALWAYS bound with the full trio infiniburn_nether/end/overworld (Via
// Protocol26_1To26_2.registerPackets addEmptyTags(BLOCK, ...) "from an older
// version, but servers may have skipped these": native 26.2 rejects
// dimension_type/overworld with `Failed to parse ... infiniburn_overworld`
// when any is unbound), and ALWAYS dialog pause_screen_additions (pvn
// 768/772 reject the dialog registry with `Unbound tags ...`). Block/item
// members are block/item ids, which live in no synced blob, so those tags
// bind empty — exactly Via's addEmptyTags backfills. Unreferenced Via
// backfill tags (1.21 enchantment/entity, 26.1 damage_type/banner_pattern)
// are omitted: binding validation is per-reference (vanilla names the one
// unbound tag), and Via's foreign member ids would be out of range against
// our first-entry subsets. Source: TagRewriter.java handle/handleGeneric,
// Protocol26_1To26_2.registerPackets, Protocol1_21_11To26_1
// (createJukeboxPlayableEntry + damage/banner addEmptyTags),
// Protocol1_20_5To1_21.onMappingDataLoaded.
std::vector<uint8_t> buildTags(int pvn) {
  size_t n = 0;
  const TagSection* secs = tagsForPvn(pvn, &n);
  proto::Writer w;
  w.varInt(static_cast<int32_t>(n));
  for (size_t i = 0; i < n; ++i) {
    w.str(secs[i].registry);
    w.varInt(static_cast<int32_t>(secs[i].nTags));
    for (size_t j = 0; j < secs[i].nTags; ++j) {
      w.str(secs[i].tags[j].id);
      w.varInt(static_cast<int32_t>(secs[i].tags[j].nMembers));
      for (size_t k = 0; k < secs[i].tags[j].nMembers; ++k)
        w.varInt(secs[i].tags[j].members[k]);
    }
  }
  return w.b;
}

// Select Known Packs body (clientbound, 766+): VarInt pack count followed by
// (namespace, id, version). Empty list is accepted; the client answers with
// its own Known Packs (serverbound 0x07) and the server then sends Finish.
// Vanilla 26.x sends minecraft:core; empty keeps us version-agnostic and the
// RegistryData blobs carry present=true + full NBT so nothing is pack-sourced.
// Source: minecraft.wiki protocol 777, Known Packs clientbound (config 0x0F).
std::vector<uint8_t> buildKnownPacks(int pvn) {
  (void)pvn;
  proto::Writer w;
  w.varInt(0);  // zero packs
  return w.b;
}

std::vector<uint8_t> buildKeepAlive(int pvn, int64_t keepAliveId) {
  (void)pvn;  // i64 body for both 764/765 and 766+.
  proto::Writer w;
  w.i64(keepAliveId);
  return w.b;
}

}  // namespace limbo::registry
