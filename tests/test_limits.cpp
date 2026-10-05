// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

// Unit tests for src/security/limits.cpp — pure logic, deterministic clock.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "security/limits.h"

using namespace limbo::security;

// Injectable fake clock: tests advance time explicitly, no sleeping.
struct FakeClock {
  double now = 1000.0;
  double operator()() { return now; }
  void advance(double seconds) { now += seconds; }
};

static void testBurstAllowed() {
  FakeClock clk;
  LimiterConfig cfg;               // defaults: 500 pkt/s, 2048*8 B/s
  Limiter lim(cfg, [&clk] { return clk.now; });

  // Burst = one second of tokens: 500 packets of 1 byte fits the packet bucket
  // (500), and 500 bytes fits comfortably inside 16384 bytes.
  int accepted = 0;
  for (int i = 0; i < 500; ++i) {
    if (lim.take(1)) ++accepted;
  }
  assert(accepted == 500);
  assert(lim.packetsAvailable() < 1e-6);
  assert(std::fabs(lim.bytesAvailable() - (16384.0 - 500.0)) < 1e-6);

  // 501st packet in the same instant must be refused (packet bucket empty)
  // even though ~15 KiB of byte budget is left.
  assert(!lim.take(1));
}

static void testByteBucketBurstAllowed() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  // Spend the entire byte burst in as few packets as possible: 2 x 8192.
  assert(lim.take(8192));
  assert(lim.take(8192));
  assert(!lim.take(1));  // byte bucket empty -> refuse regardless of packet rate
  assert(lim.packetsAvailable() > 490.0);  // packet budget barely touched
}

static void testSustainedFloodRejected() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  // 10k packets of 8 bytes as fast as possible (no time advance).
  int accepted = 0, refused = 0;
  for (int i = 0; i < 10000; ++i) {
    if (lim.take(8)) ++accepted; else ++refused;
  }
  assert(refused > 9000);
  // Packet bucket (500) is the binding constraint at 8 bytes/packet:
  // 500 packets * 8 B = 4000 B, well under the 16384 B byte budget.
  assert(accepted == 500);
}

static void testSustainedFloodRejectedByteBound() {
  FakeClock clk;
  // Big packets: byte bucket binds before the packet bucket.
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  int accepted = 0;
  for (int i = 0; i < 100; ++i) {
    if (lim.take(8192)) ++accepted;
  }
  assert(accepted == 2);  // 16384 B burst / 8192 per packet
}

static void testRefillOverTime() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  while (lim.take(1)) {}  // drain the packet bucket (500)
  assert(!lim.take(1));

  // 100 ms -> +50 packets.
  clk.advance(0.1);
  int ok = 0;
  for (int i = 0; i < 100; ++i) {
    if (lim.take(1)) ++ok; else break;
  }
  assert(ok == 50);

  // Tokens never exceed burst: wait 60 s, capacity is still 500.
  clk.advance(60.0);
  int ok2 = 0;
  for (int i = 0; i < 1000; ++i) {
    if (lim.take(1)) ++ok2; else break;
  }
  assert(ok2 == 500);
  assert(!lim.take(1));
}

static void testByteRefillOverTime() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  while (lim.take(1024)) {}  // drain 16384 B
  assert(!lim.take(1));
  // 16384 B/s -> 1 s restores exactly one burst worth.
  clk.advance(1.0);
  assert(lim.take(8192));
  assert(lim.take(8192));
  assert(!lim.take(1));
  // Half a second restores half a burst.
  clk.advance(0.5);
  assert(lim.take(8192));
  assert(!lim.take(1));
}

static void testRealisticTrafficNeverTrips() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  // 120 s of a busy-but-real client: movement + keepalive at 20 packets/s,
  // 512 B mean packet -> 10240 B/s, comfortably under the 16384 B/s budget.
  // 60 packets/s of small frames (200 B mean -> 12000 B/s) must also pass:
  // packet budget 500/s leaves 8x headroom over real client rates.
  for (int s = 0; s < 120; ++s) {
    for (int p = 0; p < 20; ++p) assert(lim.take(512));
    clk.advance(1.0);
  }
  for (int s = 0; s < 60; ++s) {
    for (int p = 0; p < 60; ++p) assert(lim.take(200));
    clk.advance(1.0);
  }
}

static void testByteBudgetBoundsMeanPacketSize() {
  // Documented trade-off: the 16384 B/s budget at the 500 pkt/s ceiling implies
  // a mean inbound packet of <=32 B. Real clients sit at 20-60 pkt/s, so the
  // byte bucket is the binding constraint for legitimate traffic only if a
  // client sustains >32 KB/s, which no vanilla limbo client does. Assert the
  // boundary is where the docs say it is.
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  for (int s = 0; s < 10; ++s) {
    // 16 KiB/s exactly = 16384 bytes per second, in 32 B packets (512 pkt/s).
    for (int p = 0; p < 500; ++p) assert(lim.take(32));
    clk.advance(1.0);
  }
  // One byte more per second must eventually be refused.
  bool refused = false;
  for (int s = 0; s < 200 && !refused; ++s) {
    for (int p = 0; p < 500; ++p) {
      if (!lim.take(33)) { refused = true; break; }
    }
    clk.advance(1.0);
  }
  assert(refused);
}

static void testPerCallCeiling() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  // A single packet larger than maxBytesPerCall is refused even on a full
  // bucket, and refusal charges nothing (bucket stays full).
  assert(!lim.take(8193));
  assert(lim.packetsAvailable() >= 499.0);
  assert(lim.bytesAvailable() >= 16384.0 - 0.5);
  assert(lim.take(8192));
}

static void testNoChargeOnRefusal() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  // Drain bytes exactly, then confirm refused packets do not eat packet budget.
  while (lim.take(2048)) {}
  assert(!lim.take(1));
  double pk = lim.packetsAvailable();
  for (int i = 0; i < 50; ++i) (void)lim.take(1);  // all refused
  assert(std::fabs(lim.packetsAvailable() - pk) < 1e-6);
}

static void testUnlimitedDimensions() {
  FakeClock clk;
  LimiterConfig cfg;
  cfg.packetsPerSecond = -1;  // disable packet bucket
  Limiter lim(cfg, [&clk] { return clk.now; });
  // Packet rate disabled, so 1-byte packets are bounded only by the byte bucket.
  int accepted = 0;
  for (int i = 0; i < 100000; ++i) {
    if (lim.take(1)) ++accepted; else break;
  }
  assert(accepted == 16384);  // full byte burst, no packet-rate cap
  assert(std::isinf(lim.packetsAvailable()));
  // Byte bucket now empty: further 1-byte packets are refused by bytes alone.
  assert(!lim.take(1));
  // And the packet dimension alone is unbounded.
  clk.advance(1.0);
  LimiterConfig onlyBytes;
  onlyBytes.packetsPerSecond = -1;
  onlyBytes.bytesPerSecond = -1;  // both disabled: nothing can ever be refused
  Limiter wide(onlyBytes, [&clk] { return clk.now; });
  for (int i = 0; i < 200000; ++i) assert(wide.take(1));
}

static void testCustomBurst() {
  FakeClock clk;
  LimiterConfig cfg;
  cfg.packetsPerSecond = 100;
  cfg.packetBurst = 10;  // small burst, same sustained rate
  Limiter lim(cfg, [&clk] { return clk.now; });
  int ok = 0;
  for (int i = 0; i < 1000; ++i) {
    if (lim.take(1)) ++ok; else break;
  }
  assert(ok == 10);
  // A full second of idling refills at the sustained rate but is capped at the
  // burst size: a small-burst policy still admits only 10 back-to-back packets
  // no matter how long the connection waited.
  clk.advance(1.0);
  ok = 0;
  for (int i = 0; i < 1000; ++i) {
    if (lim.take(1)) ++ok; else break;
  }
  assert(ok == 10);
  // Sustained 100 pkt/s is achievable by pacing: 10 packets per 100 ms.
  int paced = 0;
  for (int step = 0; step < 50; ++step) {
    clk.advance(0.1);  // 10 tokens accrue per 100 ms at 100/s
    for (int p = 0; p < 10; ++p) {
      if (lim.take(1)) ++paced; else break;
    }
  }
  assert(paced == 500);
}

static void testTakePacketsAndBytes() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  assert(lim.takePackets(500));
  assert(!lim.takePackets(1));
  clk.advance(1.0);
  assert(lim.takePackets(500));
  assert(!lim.takePackets(1));
  assert(lim.takePackets(0));  // zero always ok
}

static void testReset() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  while (lim.take(2048)) {}
  assert(!lim.take(1));
  lim.reset();
  assert(lim.take(2048));
  assert(std::fabs(lim.packetsAvailable() - 499.0) < 1e-6);
}

static void testClockNeverBackwardsDrains() {
  FakeClock clk;
  LimiterConfig cfg;
  Limiter lim(cfg, [&clk] { return clk.now; });
  while (lim.take(1)) {}
  assert(!lim.take(1));
  clk.now -= 5.0;  // clock jumps backwards
  (void)lim.take(1);
  assert(!lim.take(1));  // still refused: backwards time must not refill
  clk.now += 10.0;  // resumes from the stale baseline -> refills, capped at burst
  assert(lim.takePackets(500));  // 500 packets refilled, 500 more refused
  assert(!lim.take(1));
}

static void testTokenBucketStandalone() {
  TokenBucket b(50.0, 50.0);
  assert(!b.unlimited());
  assert(b.burst() == 50.0 && b.tokens() == 50.0);
  for (int i = 0; i < 50; ++i) assert(b.take(1));
  assert(!b.take(1));
  (void)b.take(0);   // zero is always allowed and charges nothing
  (void)b.take(-5);  // negative amounts are treated as zero, never as credit
  assert(b.tokens() <= 0.0 + 1e-9);
}

static void testSizeCaps() {
  // Strings: negatives rejected (the classic huge-unsigned-alloc bug).
  assert(!stringLenOk(-1));
  assert(!stringLenOk(-2147483647 - 1));
  assert(stringLenOk(0));
  assert(stringLenOk(32767));
  assert(!stringLenOk(32768));
  assert(stringLenOk(10, 10) && !stringLenOk(11, 10));

  // Usernames: 16 bytes.
  assert(usernameLenOk(1) && usernameLenOk(16));
  assert(!usernameLenOk(17));
  assert(!usernameLenOk(0) == false);  // 0 is a legal length; caller checks emptiness
  assert(!usernameLenOk(-5));

  // Address: 255.
  assert(addressLenOk(255) && !addressLenOk(256));

  // Byte arrays: 256 KiB.
  assert(arrayLenOk(262144));
  assert(!arrayLenOk(262145));
  assert(!arrayLenOk(-1));

  // Element counts.
  assert(arrayCountOk(0) && arrayCountOk(65536));
  assert(!arrayCountOk(65537));
  assert(!arrayCountOk(-1));

  // Frame lengths.
  assert(frameLenOk(1));
  assert(frameLenOk(8192));
  assert(!frameLenOk(0));       // no room for packet id
  assert(!frameLenOk(-1));
  assert(!frameLenOk(8193));    // over default cap
  assert(frameLenOk(20000, 1u << 20));   // allowed with a bigger cap
  assert(!frameLenOk((1 << 21), 1u << 20));
  // Hard ceiling clamps a too-large config: 4 MiB request, 1 MiB hard cap.
  assert(!frameLenOk(4 * 1024 * 1024, 8u * 1024 * 1024));
  assert(frameLenOk(1u << 20, 8u * 1024 * 1024));
}

static void testCapsAlignWithWireReaders() {
  // 1 MB string claimed inside a login packet: reader-side caps must reject
  // before any allocation happens.
  assert(!arrayLenOk(1024 * 1024));
  assert(!stringLenOk(1024 * 1024));
  // ...and a 1 MB frame is rejected at the default 8 KiB cap.
  assert(!frameLenOk(1024 * 1024));
}

int main() {
  testBurstAllowed();
  testByteBucketBurstAllowed();
  testSustainedFloodRejected();
  testSustainedFloodRejectedByteBound();
  testRefillOverTime();
  testByteRefillOverTime();
  testRealisticTrafficNeverTrips();
  testByteBudgetBoundsMeanPacketSize();
  testPerCallCeiling();
  testNoChargeOnRefusal();
  testUnlimitedDimensions();
  testCustomBurst();
  testTakePacketsAndBytes();
  testReset();
  testClockNeverBackwardsDrains();
  testTokenBucketStandalone();
  testSizeCaps();
  testCapsAlignWithWireReaders();
  printf("test_limits ok\n");
  return 0;
}