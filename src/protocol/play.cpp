#include "protocol/play.h"
#include "protocol/buffer.h"
#include "protocol/codec_data.h"
#include "protocol/versions.h"

namespace limbo::play {
namespace {

void worldNames(limbo::proto::Writer& w) {
  w.varInt(1);
  w.str("minecraft:overworld");
}

void noDeath(limbo::proto::Writer& w) { w.boolean(false); }

const uint8_t* blob(const limbo::codec::Blob& b, size_t& n) {
  n = b.size;
  return b.data;
}

}  // namespace

std::vector<uint8_t> joinGameBody(int pvn, int entityId) {
  using namespace limbo::proto;
  Writer w;
  Era e = eraForPvn(pvn);
  size_t n = 0;
  const uint8_t* cb = nullptr;
  switch (e) {
    case Era::LEGACY_1_8:
      w.i32(entityId);
      w.u8(3);          // gamemode: spectator
      w.u8(0);          // dimension: overworld (i8)
      w.u8(2);          // difficulty: normal
      w.u8(100);        // max players
      w.str("default");  // level type
      w.boolean(false);
      break;
    case Era::V1_12:
      w.i32(entityId);
      w.u8(3);
      w.i32(0);  // dimension
      w.u8(2);   // difficulty
      w.u8(100);
      w.str("default");
      w.boolean(false);
      break;
    case Era::V1_13:
      w.i32(entityId);
      w.u8(3);
      w.i32(0);  // dimension
      w.u8(2);   // difficulty
      w.u8(100);
      w.str("default");
      w.boolean(false);
      break;
    case Era::V1_14:
      w.i32(entityId);
      w.u8(3);
      w.i32(0);  // dimension
      w.u8(100);
      w.str("default");
      w.varInt(2);  // view distance
      w.boolean(false);
      break;
    case Era::V1_15:
      w.i32(entityId);
      w.u8(3);
      w.i32(0);
      w.i64(0);  // hashed seed
      w.u8(100);
      w.str("default");
      w.varInt(2);
      w.boolean(false);
      w.boolean(true);  // enable respawn screen
      break;
    case Era::V1_16_0: {
      w.i32(entityId);
      w.u8(3);
      w.u8(255);  // previous gamemode: none
      worldNames(w);
      cb = blob(codec::j116(), n);
      w.bytes(cb, n);  // dimensionCodec (1.16.0 shape)
      w.str("minecraft:overworld");  // dimension (string in 735/736)
      w.str("minecraft:overworld");  // world name
      w.i64(0);
      w.u8(100);
      w.varInt(2);
      w.boolean(false);
      w.boolean(true);
      w.boolean(false);  // isDebug
      w.boolean(false);  // isFlat
      break;
    }
    case Era::V1_16_2: {
      w.i32(entityId);
      w.boolean(false);  // isHardcore
      w.u8(3);
      w.u8(255);  // previous gamemode
      worldNames(w);
      cb = blob(codec::j1162(), n);
      w.bytes(cb, n);
      cb = blob(codec::dim_1162(), n);
      w.bytes(cb, n);  // dimension (NBT)
      w.str("minecraft:overworld");
      w.i64(0);
      w.varInt(100);
      w.varInt(2);
      w.boolean(false);
      w.boolean(true);
      w.boolean(false);
      w.boolean(false);
      break;
    }
    case Era::V1_17_1_18: {
      w.i32(entityId);
      w.boolean(false);
      w.u8(3);
      w.u8(255);  // i8 -1: previous gamemode none  // previous gamemode: none
      worldNames(w);
      cb = blob(pvn >= 757 ? codec::j118() : codec::j117(), n);
      w.bytes(cb, n);
      cb = blob(pvn >= 757 ? codec::dim_118() : codec::dim_117(), n);
      w.bytes(cb, n);
      w.str("minecraft:overworld");
      w.i64(0);
      w.varInt(100);
      w.varInt(2);
      if (pvn >= 757) w.varInt(2);  // simulation distance (1.18+)
      w.boolean(false);
      w.boolean(true);
      w.boolean(false);
      w.boolean(false);
      break;
    }
    case Era::V1_19_0:
    case Era::V1_19_2:
    case Era::V1_19_3:
    case Era::V1_19_4: {
      w.i32(entityId);
      w.boolean(false);
      w.u8(3);
      w.u8(255);  // i8 -1: previous gamemode none
      worldNames(w);
      if (pvn == 759) cb = blob(codec::j119_759(), n);
      else if (pvn == 760) cb = blob(codec::j119_760(), n);
      else cb = blob(codec::j119_7612(), n);
      w.bytes(cb, n);
      w.str("minecraft:overworld");  // world type
      w.str("minecraft:overworld");  // world name
      w.i64(0);
      w.varInt(100);
      w.varInt(2);
      w.varInt(2);
      w.boolean(false);
      w.boolean(true);
      w.boolean(false);
      w.boolean(false);
      noDeath(w);
      break;
    }
    case Era::V1_20_0_1: {
      w.i32(entityId);
      w.boolean(false);
      w.u8(3);
      w.u8(255);  // i8 -1: previous gamemode none
      worldNames(w);
      cb = blob(codec::j120_763(), n);
      w.bytes(cb, n);
      w.str("minecraft:overworld");
      w.str("minecraft:overworld");
      w.i64(0);
      w.varInt(100);
      w.varInt(2);
      w.varInt(2);
      w.boolean(false);
      w.boolean(true);
      w.boolean(false);
      w.boolean(false);
      noDeath(w);
      w.varInt(0);  // portal cooldown
      break;
    }
    case Era::V1_20_2:
    case Era::V1_20_3_4: {
      w.i32(entityId);
      w.boolean(false);
      worldNames(w);
      w.varInt(100);
      w.varInt(2);
      w.varInt(2);
      w.boolean(false);
      w.boolean(true);
      w.boolean(false);  // doLimitedCrafting
      w.str("minecraft:overworld");
      w.str("minecraft:overworld");
      w.i64(0);
      w.u8(3);
      w.u8(255);  // i8 -1: previous gamemode none
      w.boolean(false);
      w.boolean(false);
      noDeath(w);
      w.varInt(0);
      break;
    }
    default: {
      // 1.20.5+ slim JoinGame with SpawnInfo (dimension index 0 = overworld,
      // matching our single-entry dimension_type registry order).
      w.i32(entityId);
      w.boolean(false);
      worldNames(w);
      w.varInt(100);
      w.varInt(2);
      w.varInt(2);
      w.boolean(false);
      w.boolean(true);
      w.boolean(false);  // doLimitedCrafting
      w.varInt(0);       // dimension index
      w.str("minecraft:overworld");
      w.i64(0);
      w.u8(3);           // gamemode
      w.u8(255);         // previous gamemode: none
      w.boolean(false);
      w.boolean(false);
      noDeath(w);
      w.varInt(0);
      w.boolean(false);  // enforcesSecureChat
      break;
    }
  }
  return w.b;
}

std::vector<uint8_t> abilitiesBody(int pvn) {
  (void)pvn;
  limbo::proto::Writer w;
  w.u8(0x07);  // flags: invulnerable + flying + can fly
  w.f32(0.05f);
  w.f32(0.1f);
  return w.b;
}

std::vector<uint8_t> positionBody(int pvn, double x, double y, double z,
                                  float yaw, float pitch, int teleportId) {
  limbo::proto::Writer w;
  if (pvn == 47) {
    // 1.8 Player Position And Look has no teleport id.
    w.f64(x);
    w.f64(y);
    w.f64(z);
    w.f32(yaw);
    w.f32(pitch);
    w.u8(0);
  } else if (pvn >= 768) {
    // 1.21.2+: teleport ID first (Synchronize Player Position).
    w.varInt(teleportId);
    w.f64(x);
    w.f64(y);
    w.f64(z);
    w.f32(yaw);
    w.f32(pitch);
    w.u8(0);
  } else {
    w.f64(x);
    w.f64(y);
    w.f64(z);
    w.f32(yaw);
    w.f32(pitch);
    w.u8(0);
    w.varInt(teleportId);
  }
  return w.b;
}

std::vector<uint8_t> gameEventBody(int pvn, int event, float value) {
  (void)pvn;
  limbo::proto::Writer w;
  w.u8(static_cast<uint8_t>(event));
  w.f32(value);
  return w.b;
}

std::vector<uint8_t> centerChunkBody(int pvn, int chunkX, int chunkZ) {
  if (pvn < 477) return {};  // Update View Position added in 1.14
  limbo::proto::Writer w;
  w.varInt(chunkX);
  w.varInt(chunkZ);
  return w.b;
}

std::vector<uint8_t> batchStartBody(int /*pvn*/) { return {}; }

std::vector<uint8_t> batchFinishedBody(int /*pvn*/, int batchSize) {
  limbo::proto::Writer w;
  w.varInt(batchSize);
  return w.b;
}

}  // namespace limbo::play
