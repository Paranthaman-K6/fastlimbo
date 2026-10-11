// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

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
      if (pvn >= 777) {
        // 26.3+: gamemode is a VarInt Enum and previous is an Optional
        // VarInt (Via EntityPacketRewriter26_3.handleGamemodes reads BYTEs
        // from 26.2 input and writes VAR_INT/OPTIONAL_VAR_INT).
        w.varInt(3);  // spectator
        w.u8(0);      // previous: absent (none)
      } else {
        w.u8(3);    // gamemode
        w.u8(255);  // previous gamemode: none
      }
      w.boolean(false);
      w.boolean(false);
      noDeath(w);
      w.varInt(0);  // portal cooldown
      if (pvn >= 775) {
        // 26.1+ tail (Via Protocol26_1To26_2 LOGIN append passthroughs
        // Previous-gamemode..Sea-level, proving 26.1 carries sea level and
        // that nothing trails it): sea level replaces enforcesSecureChat
        // here. 26.2/26.3 append onlineMode + enforcesSecureChat after it
        // (26.2 ClientboundLoginPacket record: onlineMode, enforcesSecureChat
        // are the final fields; offline server sends both false).
        w.varInt(63);  // sea level (overworld)
        if (pvn >= 776) {
          w.boolean(false);  // onlineMode=false (offline server)
          w.boolean(false);  // enforcesSecureChat=false
        }
      } else {
        w.boolean(false);  // enforcesSecureChat (pre-26.1 tail)
      }
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
    // 1.21.2+ Synchronize Player Position (exact 1.21.2+ shape, stable
    // through 26.x): teleport ID first, then position, zero velocity,
    // rotation, and relatives as a 4-byte BE bitmask (empty = absolute).
    // Sources: Via EntityPacketRewriter1_21_2 (writes teleportId, 3xDouble,
    // 3xDouble zeros, 2xFloat, INT relatives) + vanilla 26.2 bytecode
    // (PositionMoveRotation{Vec3, Vec3, f32, f32} + INT-mask relatives).
    w.varInt(teleportId);
    w.f64(x);
    w.f64(y);
    w.f64(z);
    w.f64(0.0);
    w.f64(0.0);
    w.f64(0.0);
    w.f32(yaw);
    w.f32(pitch);
    w.i32(0);  // relatives mask: all absolute
  } else if (pvn >= 755 && pvn <= 761) {
    // 1.17..1.19.3 Player Position ends with a dismountVehicle boolean
    // (protocol_1.17.json + protocol_1.18.json packet_position; 1.19.4+
    // shapes have 7 fields per minecraft-data, and NanoLimbo gates the bool
    // to fromTo(V1_17, V1_19_3)). Via's 1.19.3->1.19.4
    // EntityPacketRewriter1_19_4 PLAYER_POSITION handler maps the 7 fields
    // above then unconditionally wrapper.read(Types.BOOLEAN) ("Dismount vehicle"); without this byte its buffer underflows. Earlier
    // Via steps (1_17_1->1_18 EntityPacketRewriter1_18, 1_19_1->1_19_3
    // EntityPacketRewriter1_19_3) register no PLAYER_POSITION handler, so no
    // step synthesizes the flag — it must come from our bytes. The boolean
    // was introduced in 1.17 (<=1.16 works via Via's 1_16_4->1_17 adder, so
    // those shapes stay untouched); 1.21.2+ uses the teleport-first layout.
    w.f64(x);
    w.f64(y);
    w.f64(z);
    w.f32(yaw);
    w.f32(pitch);
    w.u8(0);
    w.varInt(teleportId);
    w.boolean(false);  // dismountVehicle: fresh join, player is not mounted
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

std::vector<uint8_t> keepAliveBody(int pvn, int64_t keepAliveId) {
  limbo::proto::Writer w;
  if (pvn == 47) {
    w.varInt(static_cast<int32_t>(keepAliveId & 0x7FFFFFFF));
  } else {
    w.i64(keepAliveId);
  }
  return w.b;
}

std::vector<uint8_t> batchStartBody(int /*pvn*/) { return {}; }
std::vector<uint8_t> batchFinishedBody(int /*pvn*/, int batchSize) {
  limbo::proto::Writer w;
  w.varInt(batchSize);
  return w.b;
}

}  // namespace limbo::play
