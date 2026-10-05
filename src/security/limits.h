#pragma once
// Per-connection abuse limits. Pure logic: no sockets, no globals, no clock reads
// unless the caller supplies one. Lead wires this into the connection loop
// (see docs/hardening-notes.md §"Wiring").
//
// Two independent token buckets per connection:
//   - packets/sec  — caps packet *rate*, independent of payload size.
//   - bytes/sec    — caps total inbound volume so a client cannot stream
//                    oversized-but-legal payloads forever.
// Defaults follow NanoLimbo/PicoLimbo traffic comments: a real client is well
// under 500 pkt/s, and 2048*8 B/s covers chunk-heavy clients with headroom.
//
// Design rules:
//   - take() never blocks and never throws; caller decides to drop/close.
//   - All time comes from an injected clock (double seconds) so tests are
//     deterministic and there is no hidden steady_clock dependency.
//   - Rate <= 0 means "no limit" (bucket disabled) for that dimension.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace limbo::security {

// Monotonic clock in seconds. Must never go backwards.
using ClockFn = std::function<double()>;

// Steady clock in seconds; used when no clock is injected.
double steadySeconds();

struct LimiterConfig {
  double packetsPerSecond = 500.0;  // <=0 disables the packet bucket
  double bytesPerSecond = 2048.0 * 8.0;  // <=0 disables the byte bucket
  // Burst allowance. 0 => burst equals the per-second rate (classic token
  // bucket: one second of traffic may arrive at once). Explicit values let a
  // stricter policy be tested without changing rates.
  double packetBurst = 0.0;
  double byteBurst = 0.0;
  // Per-call ceiling: a single take() larger than this is refused outright
  // regardless of bucket contents. Guards against one huge packet draining a
  // full bucket in a single call.
  std::size_t maxBytesPerCall = 8192;  // 0 => no per-call ceiling
};

// One token bucket. Classic: tokens refill at `rate` per second up to `burst`.
class TokenBucket {
 public:
  TokenBucket() = default;
  TokenBucket(double ratePerSecond, double burst);

  // Configure/reset to full burst. rate<=0 disables consumption.
  void configure(double ratePerSecond, double burst);
  // Advance internal time to `nowSeconds` (seconds, monotonic). No-op if the
  // clock went backwards (keeps last known state rather than draining).
  void update(double nowSeconds);
  // Refill, then attempt to remove `amount` tokens. False => refused, nothing
  // consumed. n<=0 always succeeds.
  bool take(double amount);
  // Refill only, no consumption.
  void refill(double nowSeconds) { update(nowSeconds); }

  double rate() const { return rate_; }
  double burst() const { return burst_; }
  double tokens() const { return tokens_; }
  bool unlimited() const { return rate_ <= 0.0; }

 private:
  double rate_ = 0.0;
  double burst_ = 0.0;
  double tokens_ = 0.0;
  double last_ = 0.0;
  bool started_ = false;
};

// Combined packets/sec + bytes/sec gate for a single connection.
// Typical use per inbound packet: if (!limiter.take(body.size() + idLen)) close.
class Limiter {
 public:
  Limiter() : Limiter(LimiterConfig{}, &steadySeconds) {}
  explicit Limiter(const LimiterConfig& cfg, ClockFn clock = &steadySeconds);

  // Main entry point: account one inbound packet of nBytes. Returns false when
  // the packet should be treated as abusive (caller drops the connection).
  // Counted against BOTH buckets; on refusal neither bucket is charged.
  bool take(std::size_t nBytes);
  // Charge N packets of 0 bytes (packet-rate only). Used when a frame's
  // payload is already accounted for elsewhere.
  bool takePackets(std::size_t count);
  // Charge bytes without a packet (e.g. phase fields inside one frame).
  bool takeBytes(std::size_t nBytes);

  // Bring both buckets up to date with the injected clock without consuming.
  void tick();

  void reset();  // both buckets full, time baseline re-armed

  const LimiterConfig& config() const { return cfg_; }
  const TokenBucket& packets() const { return packets_; }
  const TokenBucket& bytes() const { return bytes_; }
  // Bytes currently left in the byte bucket (INFINITY when unlimited).
  double bytesAvailable() const { return bytes_.unlimited() ? INFINITY : bytes_.tokens(); }
  double packetsAvailable() const { return packets_.unlimited() ? INFINITY : packets_.tokens(); }

 private:
  LimiterConfig cfg_;
  ClockFn clock_;
  TokenBucket packets_;
  TokenBucket bytes_;
};

// --- size caps -------------------------------------------------------------
// Helpers mirroring vanilla-ish wire caps so packet readers can refuse absurd
// lengths before allocating. All pure comparisons, no allocation.

struct SizeCaps {
  // String length prefix, in bytes. Vanilla allows 32767 chars; we cap bytes.
  static constexpr std::size_t kMaxStringBytes = 32767;
  // Player usernames are 16 chars; bytes cap kept at 16 too (ASCII by policy).
  static constexpr std::size_t kMaxUsernameBytes = 16;
  // Address field in Handshake.
  static constexpr std::size_t kMaxAddressBytes = 255;
  // Generic VarInt-prefixed byte array (chat, NBT blobs, plugin payloads).
  static constexpr std::size_t kMaxArrayBytes = 262144;  // 256 KiB
  // Item/particle/block-state arrays use per-element bounds, not total bytes.
  static constexpr std::size_t kMaxArrayElements = 65536;
  // Single packet frame body (id + body), matching config max_packet_bytes.
  static constexpr std::size_t kMaxPacketBytes = 8192;
  // Hard ceiling regardless of config, so a bad config cannot allow OOM.
  static constexpr std::size_t kMaxPacketBytesHard = 1u << 20;  // 1 MiB
};

// True when a signed VarInt length (as read from the wire) is a legal,
// non-negative size that also fits the cap. Rejects negatives: the single most
// common source of huge unsigned allocations.
bool stringLenOk(int32_t lenBytes, std::size_t cap = SizeCaps::kMaxStringBytes);
bool usernameLenOk(int32_t lenBytes);
bool addressLenOk(int32_t lenBytes);
bool arrayLenOk(int32_t lenBytes, std::size_t cap = SizeCaps::kMaxArrayBytes);
bool arrayCountOk(int32_t count, std::size_t cap = SizeCaps::kMaxArrayElements);
// Frame length VarInt must be >0, <= maxBytes, and <= hard ceiling.
bool frameLenOk(int32_t lenBytes, std::size_t maxBytes = SizeCaps::kMaxPacketBytes);

}  // namespace limbo::security