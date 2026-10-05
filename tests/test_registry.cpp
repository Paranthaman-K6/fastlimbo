#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "protocol/registry.h"

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

  // 766+: one body per registry (dimension_type, biome, chat_type, damage_type).
  for (int pvn : {766, 767}) {
    auto blobs = buildRegistryBlobs(pvn);
    assert(blobs.size() == 4);
    const Bytes& b = blobs[0];
    const std::string regId = "minecraft:dimension_type";
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

  // --- Finish / KeepAlive bodies ---
  assert(buildFinish(764).empty());
  assert(buildFinish(767).empty());
  Bytes ka = buildKeepAlive(767, 12345);
  assert(ka == (Bytes{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x39}));
  assert(buildKeepAlive(764, -1) ==
         (Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));

  printf("test_registry ok\n");
  return 0;
}
