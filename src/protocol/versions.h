// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once
// Exact per-PVN packet IDs, verified 2026-10-05 against PrismarineJS minecraft-data
// protocol.json for the exact minor dirs (1.8, 1.12, 1.12.1, 1.12.2, 1.13, 1.14,
// 1.15, 1.16, 1.16.1, 1.16.2, 1.17, 1.17.1, 1.18, 1.18.2, 1.19, 1.19.2, 1.19.3,
// 1.19.4, 1.20, 1.20.2, 1.20.3, 1.20.5, 1.21.1, 1.21.3, 1.21.4, 1.21.5, 1.21.6,
// 1.21.8, 1.21.9, 1.21.11, 26.1) + NanoLimbo State.java cross-check.
// Minors without own data dir reuse the nearest verified row (see REUSE below).
// Logic must branch on helpers here, never on hardcoded IDs elsewhere.
#include <string>

namespace limbo::proto {

// Wire-shape eras (body encoding, not IDs).
enum class Era {
  LEGACY_1_8,      // PVN 47
  V1_12,           // 335-340
  V1_13,           // 393-404
  V1_14,           // 477-480
  V1_15,           // 485-498
  V1_16_0,         // 735-736
  V1_16_2,         // 751-754
  V1_17_1_18,      // 755-758
  V1_19_0,         // 759
  V1_19_2,         // 760
  V1_19_3,         // 761
  V1_19_4,         // 762
  V1_20_0_1,       // 763
  V1_20_2,         // 764 (Configuration introduced)
  V1_20_3_4,       // 765
  V1_20_5_6,       // 766
  V1_21_0_1,       // 767
  V1_21_2_3,       // 768-769
  V1_21_5_8,       // 770-772
  V1_21_9_10,      // 773
  V1_21_11,        // 774
  V1_26_1,         // 775
  V1_26_2,         // 776 (packet IDs identical to 775 per ViaVersion v26_1to26_2)
  V1_26_3,         // 777 (IDs shifted; see playIds — ViaVersion v26_2to26_3 enums)
  UNKNOWN,
};

inline Era eraForPvn(int pvn) {
  if (pvn == 47) return Era::LEGACY_1_8;
  if (pvn == 335 || pvn == 338 || pvn == 340) return Era::V1_12;
  if (pvn == 393 || pvn == 401 || pvn == 404) return Era::V1_13;  // releases only
  if (pvn == 477 || pvn == 480 || pvn == 485 || pvn == 490 || pvn == 498) return Era::V1_14;
  if (pvn == 573 || pvn == 575 || pvn == 578) return Era::V1_15;
  if (pvn == 735 || pvn == 736) return Era::V1_16_0;
  if (pvn == 751 || pvn == 753 || pvn == 754) return Era::V1_16_2;
  if (pvn >= 755 && pvn <= 758) return Era::V1_17_1_18;
  if (pvn == 759) return Era::V1_19_0;
  if (pvn == 760) return Era::V1_19_2;
  if (pvn == 761) return Era::V1_19_3;
  if (pvn == 762) return Era::V1_19_4;
  if (pvn == 763) return Era::V1_20_0_1;
  if (pvn == 764) return Era::V1_20_2;
  if (pvn == 765) return Era::V1_20_3_4;
  if (pvn == 766) return Era::V1_20_5_6;
  if (pvn == 767) return Era::V1_21_0_1;
  if (pvn == 768 || pvn == 769) return Era::V1_21_2_3;
  if (pvn >= 770 && pvn <= 772) return Era::V1_21_5_8;
  if (pvn == 773) return Era::V1_21_9_10;
  if (pvn == 774) return Era::V1_21_11;
  if (pvn == 775) return Era::V1_26_1;
  if (pvn == 776) return Era::V1_26_2;
  if (pvn == 777) return Era::V1_26_3;
  return Era::UNKNOWN;
}

// Highest supported PVN (ViaVersion-verified IDs; newest checked: 26.3).
inline int maxSupportedPvn() { return 777; }

inline bool hasConfiguration(int pvn) { return pvn >= 764; }
inline bool hasLoginPlugin(int pvn) { return pvn >= 393; }  // 1.13+; modern forwarding needs it
inline bool needsGameEvent13(int pvn) { return pvn >= 765; }  // 1.20.3+

// REUSE (no own data dir, verified same-or-nearest row):
// 338->340 row, 401/404->393 row, 480->477 row, 490/498->485 row,
// 736->735 row, 753/754->751 row, 756->755 row, 758->757 row.

struct PlayIds {
  int joinGame, keepAlive, position, chunk, disconnect, abilities, gameEvent, centerChunk;
  int batchStart, batchFinished;  // -1 before 1.20.2
};

inline PlayIds playIds(int pvn) {
  Era e = eraForPvn(pvn);
  const int NO = -1;
  switch (e) {
    case Era::LEGACY_1_8: return {0x01, 0x00, 0x08, 0x21, 0x40, 0x39, 0x2B, NO, NO, NO};
    case Era::V1_12:
      if (pvn == 335) return {0x23, 0x1F, 0x2E, 0x20, 0x1A, 0x2B, 0x1E, NO, NO, NO};
      return {0x23, 0x1F, 0x2F, 0x20, 0x1A, 0x2C, 0x1E, NO, NO, NO};  // 338/340
    case Era::V1_13:   return {0x25, 0x21, 0x32, 0x22, 0x1B, 0x2E, 0x20, NO, NO, NO};
    case Era::V1_14:   return {0x25, 0x20, 0x35, 0x21, 0x1A, 0x31, 0x1E, 0x40, NO, NO};
    case Era::V1_15:   return {0x26, 0x21, 0x36, 0x22, 0x1B, 0x32, 0x1F, 0x41, NO, NO};
    case Era::V1_16_0: return {0x25, 0x20, 0x35, 0x21, 0x1A, 0x31, 0x1E, 0x40, NO, NO};
    case Era::V1_16_2: return {0x24, 0x1F, 0x34, 0x20, 0x19, 0x30, 0x1D, 0x40, NO, NO};
    case Era::V1_17_1_18: return {0x26, 0x21, 0x38, 0x22, 0x1A, 0x32, 0x1E, 0x49, NO, NO};
    case Era::V1_19_0: return {0x23, 0x1E, 0x36, 0x1F, 0x17, 0x2F, 0x1B, 0x48, NO, NO};
    case Era::V1_19_2: return {0x25, 0x20, 0x39, 0x21, 0x19, 0x31, 0x1D, 0x4B, NO, NO};
    case Era::V1_19_3: return {0x24, 0x1F, 0x38, 0x20, 0x17, 0x30, 0x1C, 0x4A, NO, NO};
    case Era::V1_19_4: return {0x28, 0x23, 0x3C, 0x24, 0x1A, 0x34, 0x1F, 0x4E, NO, NO};
    case Era::V1_20_0_1: return {0x28, 0x23, 0x3C, 0x24, 0x1A, 0x34, NO, 0x4E, NO, NO};
    case Era::V1_20_2: return {0x29, 0x24, 0x3E, 0x25, 0x1B, 0x36, 0x20, 0x50, 0x0D, 0x0C};
    case Era::V1_20_3_4: return {0x29, 0x24, 0x3E, 0x25, 0x1B, 0x36, 0x20, 0x52, 0x0D, 0x0C};
    case Era::V1_20_5_6: return {0x2B, 0x26, 0x40, 0x27, 0x1D, 0x38, 0x22, 0x54, 0x0D, 0x0C};
    case Era::V1_21_0_1: return {0x2B, 0x26, 0x40, 0x27, 0x1D, 0x38, 0x22, 0x54, 0x0D, 0x0C};
    case Era::V1_21_2_3: return {0x2C, 0x27, 0x42, 0x28, 0x1D, 0x3A, 0x23, 0x58, 0x0D, 0x0C};
    case Era::V1_21_5_8: return {0x2B, 0x26, 0x41, 0x27, 0x1C, 0x39, 0x22, 0x57, 0x0C, 0x0B};
    case Era::V1_21_9_10: return {0x30, 0x2B, 0x46, 0x2C, 0x20, 0x3E, 0x26, 0x5C, 0x0C, 0x0B};
    case Era::V1_21_11: return {0x30, 0x2B, 0x46, 0x2C, 0x20, 0x3E, 0x26, 0x5C, 0x0C, 0x0B};
    case Era::V1_26_1: return {0x31, 0x2C, 0x48, 0x2D, 0x20, 0x40, 0x26, 0x5E, 0x0C, 0x0B};
    case Era::V1_26_2: return {0x31, 0x2C, 0x48, 0x2D, 0x20, 0x40, 0x26, 0x5E, 0x0C, 0x0B};
    case Era::V1_26_3: return {0x32, 0x2D, 0x49, 0x2E, 0x20, 0x41, 0x27, 0x60, 0x0C, 0x0B};
    default:           return {0x2B, 0x26, 0x40, 0x27, 0x1D, 0x38, 0x22, 0x54, 0x0D, 0x0C};
  }
}

// Serverbound Play movement IDs (for the spawn freeze: teleport-back on
// position-carrying packets, silent accept of teleport confirms). -1 = the
// packet does not exist in that era. Verified against minecraft-data
// protocol.json per exact minor + ViaVersion ServerboundPackets enums;
// only position-carrying inbound (pos/posRot/vehicle) gets a reply.
struct MoveIds {
  int confirm, pos, posRot, vehicle;
};

inline MoveIds playMovementServerbound(int pvn) {
  Era e = eraForPvn(pvn);
  const int NO = -1;
  switch (e) {
    case Era::LEGACY_1_8: return {NO, 0x04, 0x06, NO};
    case Era::V1_12:
      if (pvn == 335) return {0x00, 0x0E, 0x0F, 0x11};
      return {0x00, 0x0D, 0x0E, 0x10};  // 338/340
    case Era::V1_13:   return {0x00, 0x10, 0x11, 0x13};
    case Era::V1_14:   return {0x00, 0x11, 0x12, 0x15};
    case Era::V1_15:   return {0x00, 0x11, 0x12, 0x15};
    case Era::V1_16_0: return {0x00, 0x12, 0x13, 0x16};
    case Era::V1_16_2: return {0x00, 0x12, 0x13, 0x16};
    case Era::V1_17_1_18: return {0x00, 0x11, 0x12, 0x15};
    case Era::V1_19_0: return {0x00, 0x13, 0x14, 0x17};
    case Era::V1_19_2: return {0x00, 0x14, 0x15, 0x18};
    case Era::V1_19_3: return {0x00, 0x13, 0x14, 0x17};
    case Era::V1_19_4: return {0x00, 0x14, 0x15, 0x18};
    case Era::V1_20_0_1: return {0x00, 0x14, 0x15, 0x18};
    case Era::V1_20_2: return {0x00, 0x16, 0x17, 0x1A};
    case Era::V1_20_3_4: return {0x00, 0x17, 0x18, 0x1B};
    case Era::V1_20_5_6: return {0x00, 0x1A, 0x1B, 0x1E};
    case Era::V1_21_0_1: return {0x00, 0x1A, 0x1B, 0x1E};
    case Era::V1_21_2_3: return {0x00, 0x1C, 0x1D, 0x20};
    case Era::V1_21_5_8:
      if (pvn == 770) return {0x00, 0x1C, 0x1D, 0x20};
      return {0x00, 0x1D, 0x1E, 0x21};  // 771/772
    case Era::V1_21_9_10: return {0x00, 0x1D, 0x1E, 0x21};
    case Era::V1_21_11: return {0x00, 0x1D, 0x1E, 0x21};
    case Era::V1_26_1: case Era::V1_26_2: case Era::V1_26_3:
      return {0x00, 0x1E, 0x1F, 0x22};
    default: return {NO, NO, NO, NO};
  }
}

// Serverbound Play KeepAlive ID (to accept client echo).
inline int playKeepAliveServerbound(int pvn) {
  Era e = eraForPvn(pvn);
  switch (e) {
    case Era::LEGACY_1_8: return 0x00;
    case Era::V1_12: return (pvn == 335) ? 0x0C : 0x0B;
    case Era::V1_13: return 0x0E;
    case Era::V1_14: case Era::V1_15: return 0x0F;
    case Era::V1_16_0: case Era::V1_16_2: return 0x10;
    case Era::V1_17_1_18: return 0x0F;
    case Era::V1_19_0: case Era::V1_19_3: return 0x11;
    case Era::V1_19_2: case Era::V1_19_4: case Era::V1_20_0_1: return 0x12;
    case Era::V1_20_2: return 0x14;
    case Era::V1_20_3_4: return 0x15;
    case Era::V1_20_5_6: case Era::V1_21_0_1: return 0x18;
    case Era::V1_21_2_3: return 0x1A;
    case Era::V1_21_5_8: return (pvn == 770) ? 0x1A : 0x1B;  // 0x1B per Via
      // ServerboundPackets1_21_6 + minecraft-data 1.21.6/1.21.8 (0x1A is 770-only)
    case Era::V1_21_9_10: case Era::V1_21_11: return 0x1B;
    case Era::V1_26_1: case Era::V1_26_2: case Era::V1_26_3: return 0x1C;
    default: return 0x18;
  }
}

// Standalone Update Light packet id. Only 1.17/1.17.1 (755/756) has it
// (0x25 per the 1.17 protocol.json play mapper; 1.18+ embeds light in the
// chunk, older eras carry light inside Map Chunk). -1 = no such packet.
inline int playUpdateLightId(int pvn) {
  return (pvn == 755 || pvn == 756) ? 0x25 : -1;
}

inline const char* versionName(int pvn) {
  switch (pvn) {
    case 47: return "1.8.x"; case 335: return "1.12"; case 338: return "1.12.1";
    case 340: return "1.12.2"; case 393: return "1.13"; case 401: return "1.13.1";
    case 404: return "1.13.2"; case 477: return "1.14"; case 480: return "1.14.1";
    case 485: return "1.14.2"; case 490: return "1.14.3"; case 498: return "1.14.4";
    case 573: return "1.15"; case 575: return "1.15.1"; case 578: return "1.15.2";
    case 735: return "1.16"; case 736: return "1.16.1"; case 751: return "1.16.2";
    case 753: return "1.16.3"; case 754: return "1.16.4/1.16.5"; case 755: return "1.17";
    case 756: return "1.17.1"; case 757: return "1.18/1.18.1";
    case 758: return "1.18.2"; case 759: return "1.19"; case 760: return "1.19.2";
    case 761: return "1.19.3"; case 762: return "1.19.4"; case 763: return "1.20/1.20.1";
    case 764: return "1.20.2"; case 765: return "1.20.3/1.20.4"; case 766: return "1.20.5/1.20.6";
    case 767: return "1.21/1.21.1"; case 768: return "1.21.2/1.21.3"; case 769: return "1.21.4";
    case 770: return "1.21.5"; case 771: return "1.21.6"; case 772: return "1.21.7/1.21.8";
    case 773: return "1.21.9/1.21.10"; case 774: return "1.21.11+"; case 775: return "26.1";
    case 776: return "26.2"; case 777: return "26.3";
    default: return "unknown";
  }
}

}  // namespace limbo::proto
