// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include <cassert>
#include <cstdio>
#include "protocol/buffer.h"
#include "protocol/play.h"
#include "protocol/versions.h"

int main() {
  using namespace limbo::play;
  // Non-empty JoinGame body.
  auto jg = joinGameBody(767, 1);
  assert(!jg.empty());
  // --- 26.x JoinGame tail (appended) ---
  // 26.1+: portal cooldown, sea level 63 (no enforcesSecureChat — Via
  // Protocol26_1To26_2 LOGIN append consumes Previous-gamemode..Sea-level
  // from its 26.1 input, proving nothing trails sea level there).
  // 26.2+: onlineMode=false + enforcesSecureChat=false (26.2
  // ClientboundLoginPacket record ends with exactly these two booleans;
  // verified with javap against the vanilla 26.2 client jar).
  // 26.3+: same tail; gamemode becomes VarInt + Optional previous
  // (Via EntityPacketRewriter26_3.handleGamemodes). Pre-26.1 keeps the
  // old portal + secureChat tail.
  for (int pvn : {775, 776, 777}) {
    auto b = joinGameBody(pvn, 1);
    assert(!b.empty());
    size_t n = b.size();
    if (pvn == 775) {
      assert(b[n - 1] == 63);  // ends at sea level
    } else {
      assert(b[n - 3] == 63);  // sea level
      assert(b[n - 2] == 0x00 && b[n - 1] == 0x00);  // onlineMode+secureChat
    }
  }
  // 777 gamemode encoding is VarInt(3) + absent previous (single 0x00);
  // 776 and below keep byte gamemodes (Via reads BYTE from 26.2 input).
  {
    auto b777 = joinGameBody(777, 1);
    auto b776 = joinGameBody(776, 1);
    assert(b777.size() == b776.size());  // 0x03 vs 0x03, 0x00 vs 0xFF equal len
    // 767 keeps the legacy tail (portal 0, secureChat false).
    auto b767 = joinGameBody(767, 1);
    assert(b767[b767.size() - 1] == 0x00 && b767[b767.size() - 2] == 0x00);
  }
  // GameEvent13 gated by needsGameEvent13(pvn).
  assert(limbo::proto::needsGameEvent13(765));
  assert(!limbo::proto::needsGameEvent13(764));
  auto ge13 = gameEventBody(765, 13, 0.0f);
  assert(!ge13.empty());
  // --- 1.17 (755) position body: exact 35 bytes incl. trailing bool ---
  // Shape per protocol_1.17.json packet_position: f64 x3, f32 x2, i8 flags,
  // VarInt teleportId, bool dismountVehicle. Via's 1.19.3->1.19.4
  // EntityPacketRewriter1_19_4 PLAYER_POSITION handler reads that trailing
  // boolean; without it the 26.2 ViaFabricPlus path underflows.
  {
    auto pb = positionBody(755, 8.5, 400.0, 8.5, 0.0f, 0.0f, 1);
    assert(pb.size() == 35);  // 8*3 + 4*2 + 1 + 1 + 1
    assert(pb.back() == 0x00);  // dismountVehicle=false
    limbo::proto::Reader r(pb);
    assert(r.f64() == 8.5);
    assert(r.f64() == 400.0);
    assert(r.f64() == 8.5);
    assert(r.f32() == 0.0f);
    assert(r.f32() == 0.0f);
    assert(r.u8() == 0);
    assert(r.varInt() == 1);
    assert(r.boolean() == false);
    assert(r.ok && r.remaining() == 0);
    // 756-761 identical; 762+ drop the bool (7-field shapes per
    // minecraft-data 1.19.4+; NanoLimbo fromTo(V1_17, V1_19_3)).
    assert(positionBody(756, 8.5, 400.0, 8.5, 0.0f, 0.0f, 1) == pb);
    assert(positionBody(761, 8.5, 400.0, 8.5, 0.0f, 0.0f, 1) == pb);
    assert(positionBody(757, 8.5, 400.0, 8.5, 0.0f, 0.0f, 1) == pb);
    assert(positionBody(762, 8.5, 400.0, 8.5, 0.0f, 0.0f, 1).size() == 34);
    assert(positionBody(767, 8.5, 400.0, 8.5, 0.0f, 0.0f, 1).size() == 34);
    // 754 and below keep the old 34-byte shape (Via's 1_16_4->1_17 adder).
    assert(positionBody(754, 8.5, 400.0, 8.5, 0.0f, 0.0f, 1).size() == 34);
    // 768+ teleport-first 1.21.2+ shape: varint id + 3x f64 pos + 3x f64
    // zero velocity + 2x f32 rot + i32 relatives mask = 61 bytes.
    {
      auto pm = positionBody(776, 8.5, 320.0, 8.5, 0.0f, 0.0f, 1);
      assert(pm.size() == 61);
      limbo::proto::Reader r(pm);
      assert(r.varInt() == 1);
      assert(r.f64() == 8.5 && r.f64() == 320.0 && r.f64() == 8.5);
      assert(r.f64() == 0.0 && r.f64() == 0.0 && r.f64() == 0.0);
      assert(r.f32() == 0.0f && r.f32() == 0.0f);
      assert(r.i32() == 0);
      assert(r.ok && r.remaining() == 0);
      assert(positionBody(768, 8.5, 320.0, 8.5, 0.0f, 0.0f, 1) == pm);
    }
    // Update Light id gating: 0x25 on 755/756 only.
    assert(limbo::proto::playUpdateLightId(755) == 0x25);
    assert(limbo::proto::playUpdateLightId(756) == 0x25);
    assert(limbo::proto::playUpdateLightId(757) == -1);
    assert(limbo::proto::playUpdateLightId(763) == -1);
  }
  printf("test_play ok\n");
  return 0;
}
