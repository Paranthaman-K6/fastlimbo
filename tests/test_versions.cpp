// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

// Exact per-PVN packet IDs verified against minecraft-data protocol.json
// (exact-minor dirs). Guards the tables in src/protocol/versions.h.
#include <cassert>
#include <cstdio>
#include "protocol/versions.h"

int main() {
  using namespace limbo::proto;
  auto eq = [](PlayIds a, PlayIds b) {
    return a.joinGame == b.joinGame && a.keepAlive == b.keepAlive &&
           a.position == b.position && a.chunk == b.chunk &&
           a.disconnect == b.disconnect && a.abilities == b.abilities &&
           a.gameEvent == b.gameEvent && a.centerChunk == b.centerChunk &&
           a.batchStart == b.batchStart && a.batchFinished == b.batchFinished;
  };
  // 1.8
  assert(eq(playIds(47), {0x01, 0x00, 0x08, 0x21, 0x40, 0x39, 0x2B, -1, -1, -1}));
  // 1.12 split: 335 vs 338/340
  assert(playIds(335).position == 0x2E && playIds(335).abilities == 0x2B);
  assert(playIds(340).position == 0x2F && playIds(340).abilities == 0x2C);
  assert(playKeepAliveServerbound(335) == 0x0C && playKeepAliveServerbound(340) == 0x0B);
  // 1.13-1.15 distinct rows (exact release PVNs only)
  assert(playIds(393).position == 0x32 && playIds(393).abilities == 0x2E);
  assert(playIds(477).position == 0x35 && playIds(477).centerChunk == 0x40);
  assert(playIds(481).position == 0x35 && playIds(485).position == 0x35);
  assert(playIds(573).position == 0x36 && playIds(573).centerChunk == 0x41);
  assert(playIds(575).position == 0x36);
  assert(eraForPvn(498) == Era::UNKNOWN);  // no release uses 498
  assert(playKeepAliveServerbound(393) == 0x0E && playKeepAliveServerbound(477) == 0x0F);
  // 1.16 split: 735 vs 751+
  assert(playIds(735).joinGame == 0x25 && playIds(735).position == 0x35);
  assert(playIds(754).joinGame == 0x24 && playIds(754).position == 0x34);
  // 1.17-1.18: KA 0x21, center 0x49
  assert(playIds(757).keepAlive == 0x21 && playIds(757).centerChunk == 0x49);
  assert(playKeepAliveServerbound(757) == 0x0F);
  // 1.19 minors all distinct
  assert(playIds(759).joinGame == 0x23 && playIds(760).joinGame == 0x25);
  assert(playIds(761).joinGame == 0x24 && playIds(762).joinGame == 0x28);
  // 1.20.2/1.20.3 centers differ; batch present from 764
  assert(playIds(764).centerChunk == 0x50 && playIds(765).centerChunk == 0x52);
  assert(playIds(764).batchStart == 0x0D && playIds(764).batchFinished == 0x0C);
  assert(playKeepAliveServerbound(764) == 0x14 && playKeepAliveServerbound(765) == 0x15);
  // 767 exact; 768/769 disc 0x1D center 0x58
  assert(eq(playIds(767), {0x2B, 0x26, 0x40, 0x27, 0x1D, 0x38, 0x22, 0x54, 0x0D, 0x0C}));
  assert(playIds(768).disconnect == 0x1D && playIds(768).centerChunk == 0x58);
  // 770-772 row; 773/774 row; 775 row
  assert(playIds(770).position == 0x41 && playIds(770).centerChunk == 0x57);
  assert(playKeepAliveServerbound(770) == 0x1A);
  assert(playIds(773).joinGame == 0x30 && playIds(773).centerChunk == 0x5C);
  assert(playIds(775).joinGame == 0x31 && playIds(775).position == 0x48);
  assert(playKeepAliveServerbound(775) == 0x1C);
  // gating
  assert(hasConfiguration(764) && !hasConfiguration(763));
  assert(needsGameEvent13(765) && !needsGameEvent13(764));
  assert(eraForPvn(776) == Era::UNKNOWN && maxSupportedPvn() == 775);
  printf("test_versions ok\n");
  return 0;
}
