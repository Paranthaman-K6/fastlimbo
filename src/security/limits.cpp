// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "security/limits.h"

#include <chrono>
#include <cmath>

namespace limbo::security {

double steadySeconds() {
  using namespace std::chrono;
  return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

TokenBucket::TokenBucket(double ratePerSecond, double burst) { configure(ratePerSecond, burst); }

void TokenBucket::configure(double ratePerSecond, double burst) {
  rate_ = ratePerSecond;
  burst_ = (burst > 0.0) ? burst : ratePerSecond;
  if (burst_ < 0.0) burst_ = 0.0;
  // Unbounded rate means the bucket never refills but never limits either;
  // keep tokens at burst for introspection consistency.
  tokens_ = burst_;
  last_ = 0.0;
  started_ = false;
}

void TokenBucket::update(double nowSeconds) {
  if (unlimited()) {
    last_ = nowSeconds;
    started_ = true;
    return;
  }
  if (!started_) {
    // First observation: the connection starts with a full bucket and this
    // instant becomes the baseline.
    last_ = nowSeconds;
    started_ = true;
    return;
  }
  if (nowSeconds <= last_) return;  // non-monotonic clock: ignore, never drain
  const double dt = nowSeconds - last_;
  last_ = nowSeconds;
  if (rate_ <= 0.0) return;
  tokens_ += dt * rate_;
  if (tokens_ > burst_) tokens_ = burst_;
}

bool TokenBucket::take(double amount) {
  if (unlimited()) return true;
  if (amount <= 0.0) return true;
  if (tokens_ + 1e-9 < amount) return false;  // refused: charge nothing
  tokens_ -= amount;
  return true;
}

Limiter::Limiter(const LimiterConfig& cfg, ClockFn clock)
    : cfg_(cfg), clock_(clock ? std::move(clock) : ClockFn(&steadySeconds)) {
  packets_.configure(cfg_.packetsPerSecond, cfg_.packetBurst);
  bytes_.configure(cfg_.bytesPerSecond, cfg_.byteBurst);
  // Re-arm both buckets against the injected clock right away so the very
  // first take() sees a full burst rather than an unset baseline.
  tick();
}

void Limiter::tick() {
  const double now = clock_ ? clock_() : steadySeconds();
  packets_.update(now);
  bytes_.update(now);
}

bool Limiter::take(std::size_t nBytes) {
  tick();
  if (cfg_.maxBytesPerCall > 0 && nBytes > cfg_.maxBytesPerCall) return false;
  // Two-phase: check both buckets, commit only if both allow it, so a refused
  // packet does not silently eat the packet-rate budget.
  if (!packets_.unlimited() && packets_.tokens() + 1e-9 < 1.0) return false;
  if (!bytes_.unlimited()) {
    const double want = static_cast<double>(nBytes);
    if (bytes_.tokens() + 1e-9 < want) return false;
  }
  packets_.take(1.0);
  bytes_.take(static_cast<double>(nBytes));
  return true;
}

bool Limiter::takePackets(std::size_t count) {
  tick();
  if (count == 0) return true;
  const double want = static_cast<double>(count);
  if (!packets_.unlimited() && packets_.tokens() + 1e-9 < want) return false;
  packets_.take(want);
  return true;
}

bool Limiter::takeBytes(std::size_t nBytes) {
  tick();
  if (cfg_.maxBytesPerCall > 0 && nBytes > cfg_.maxBytesPerCall) return false;
  if (bytes_.unlimited()) return true;
  if (bytes_.tokens() + 1e-9 < static_cast<double>(nBytes)) return false;
  bytes_.take(static_cast<double>(nBytes));
  return true;
}

void Limiter::reset() {
  packets_.configure(cfg_.packetsPerSecond, cfg_.packetBurst);
  bytes_.configure(cfg_.bytesPerSecond, cfg_.byteBurst);
  tick();
}

bool stringLenOk(int32_t lenBytes, std::size_t cap) {
  if (lenBytes < 0) return false;
  return static_cast<std::size_t>(lenBytes) <= cap;
}

bool usernameLenOk(int32_t lenBytes) { return stringLenOk(lenBytes, SizeCaps::kMaxUsernameBytes); }

bool addressLenOk(int32_t lenBytes) { return stringLenOk(lenBytes, SizeCaps::kMaxAddressBytes); }

bool arrayLenOk(int32_t lenBytes, std::size_t cap) { return stringLenOk(lenBytes, cap); }

bool arrayCountOk(int32_t count, std::size_t cap) {
  if (count < 0) return false;
  return static_cast<std::size_t>(count) <= cap;
}

bool frameLenOk(int32_t lenBytes, std::size_t maxBytes) {
  if (lenBytes <= 0) return false;  // 0-byte frame has no room for a packet id
  if (maxBytes > SizeCaps::kMaxPacketBytesHard) maxBytes = SizeCaps::kMaxPacketBytesHard;
  return static_cast<std::size_t>(lenBytes) <= maxBytes;
}

}  // namespace limbo::security