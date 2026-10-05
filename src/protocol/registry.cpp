#include "protocol/registry.h"

#include <cstring>
#include <vector>

#include "protocol/buffer.h"
#include "protocol/codec_data.h"

namespace limbo::registry {

// Clientbound Configuration ids verified against minecraft-data protocol.json
// (1.20.2/1.20.3/1.20.5/1.21.1/1.21.4 + NanoLimbo State.java cross-check).
ConfigIds configIds(int pvn) {
  if (pvn >= 766) return {0x07, 0x03, 0x04, 0x02};
  return {0x05, 0x02, 0x03, 0x01};
}

int configTagsId(int pvn) {
  if (pvn >= 766) return 0x0D;
  if (pvn >= 765) return 0x09;  // 1.20.3+ inserted remove/add resource pack
  if (pvn >= 764) return 0x08;
  return 0;
}

int configKnownPacksId(int pvn) { return pvn >= 766 ? 0x0E : 0; }
int configKnownPacksServerboundId(int pvn) { return pvn >= 766 ? 0x07 : 0; }

int configFinishAckId(int pvn) { return pvn >= 766 ? 0x03 : 0x02; }

namespace {

// Copy a generated blob into a byte vector.
std::vector<uint8_t> blobBytes(codec::Blob b) {
  return std::vector<uint8_t>(b.data, b.data + b.size);
}

}  // namespace

std::vector<std::vector<uint8_t>> buildRegistryBlobs(int pvn) {
  // Real registry data from minecraft-data loginPacket.json (filtered to
  // overworld + plains + chat + damage; vanilla IDs preserved). See
  // tools/gen_codec.py for provenance and the reuse map.
  std::vector<std::vector<uint8_t>> out;
  if (pvn == 764 || pvn == 765) {
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
  else target = "e775";
  // One RegistryData body per registry, in vanilla order.
  struct Entry {
    const char* suffix;
    codec::Blob blob;
  };
  std::vector<Entry> entries;
  if (!std::strcmp(target, "e766")) {
    entries = {{"dimension_type", codec::e766_dimension_type()},
               {"biome", codec::e766_biome()},
               {"chat_type", codec::e766_chat_type()},
               {"damage_type", codec::e766_damage_type()}};
  } else if (!std::strcmp(target, "e767")) {
    entries = {{"dimension_type", codec::e767_dimension_type()},
               {"biome", codec::e767_biome()},
               {"chat_type", codec::e767_chat_type()},
               {"damage_type", codec::e767_damage_type()}};
  } else if (!std::strcmp(target, "e769")) {
    entries = {{"dimension_type", codec::e769_dimension_type()},
               {"biome", codec::e769_biome()},
               {"chat_type", codec::e769_chat_type()},
               {"damage_type", codec::e769_damage_type()}};
  } else if (!std::strcmp(target, "e770")) {
    entries = {{"dimension_type", codec::e770_dimension_type()},
               {"biome", codec::e770_biome()},
               {"chat_type", codec::e770_chat_type()},
               {"damage_type", codec::e770_damage_type()}};
  } else if (!std::strcmp(target, "e773")) {
    entries = {{"dimension_type", codec::e773_dimension_type()},
               {"biome", codec::e773_biome()},
               {"chat_type", codec::e773_chat_type()},
               {"damage_type", codec::e773_damage_type()}};
  } else if (!std::strcmp(target, "e774")) {
    entries = {{"dimension_type", codec::e774_dimension_type()},
               {"biome", codec::e774_biome()},
               {"chat_type", codec::e774_chat_type()},
               {"damage_type", codec::e774_damage_type()}};
  } else {
    entries = {{"dimension_type", codec::e775_dimension_type()},
               {"biome", codec::e775_biome()},
               {"chat_type", codec::e775_chat_type()},
               {"damage_type", codec::e775_damage_type()}};
  }
  for (auto& e : entries) {
    if (e.blob.size == 0) continue;  // registry absent in source data
    out.push_back(blobBytes(e.blob));
  }
  return out;
}

std::vector<uint8_t> buildFinish(int pvn) {
  (void)pvn;  // Finish Configuration has no fields in any era.
  return {};
}

std::vector<uint8_t> buildKeepAlive(int pvn, int64_t keepAliveId) {
  (void)pvn;  // i64 body for both 764/765 and 766+.
  proto::Writer w;
  w.i64(keepAliveId);
  return w.b;
}

}  // namespace limbo::registry
