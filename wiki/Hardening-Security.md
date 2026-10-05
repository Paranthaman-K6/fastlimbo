# Hardening & Security

> **Sources:** [docs/hardening-notes.md](../docs/hardening-notes.md), [`src/security/limits.h/.cpp`](../src/security/limits.h), [`tests/fuzz/`](../tests/fuzz/)

---

## Threat Model

| Attacker | Vector | Goal |
|----------|--------|------|
| Malicious client | Direct (if exposed) or via proxy | Crash, DoS, memory exhaust, info leak |
| Compromised proxy | Trusted TCP connection | Inject malformed packets |
| Buggy proxy | Trusted TCP connection | Send oversized/garbage packets |
| Network | TCP stream | Flood, slowloris, fragmentation |

**Assumptions:**
- limbo-c++ runs **behind a trusted proxy** (Velocity) on localhost/LAN
- Proxy terminates TLS, auth, compression
- limbo-c++ sees raw TCP, no encryption

---

## Defense Layers

### 1. Connection Limits

| Limit | Config Key | Default | Action on Exceed |
|-------|------------|---------|------------------|
| Max concurrent players | `max_players` | 100 | `close(fd)` immediately after `accept()` |
| Read timeout | `read_timeout_ms` | 30000 | `close(fd)` on `recv()` timeout |
| Max packet bytes | `max_packet_bytes` | 8192 | Disconnect on decode |
| Max packets/sec | `max_packets_per_sec` | 200 | Disconnect |
| Max bytes/sec | `max_bytes_per_sec` | 1048576 | Disconnect |

**Implementation:** [`src/security/limits.h/.cpp`](../src/security/limits.h) — sliding window (1s buckets) per connection.

### 2. Protocol Parsing Hardening

| Check | Location | Behavior |
|-------|----------|----------|
| VarInt length ≤ 5 bytes | `buffer.cpp:readVarInt` | Disconnect on overflow |
| String length ≤ 32767 | `buffer.cpp:readString` | Disconnect on oversize |
| Packet ID known for era | `connection.cpp` | Disconnect on unknown |
| Packet body length matches | `buffer.cpp` | Disconnect on mismatch |
| NBT depth ≤ 64 | `nbt.cpp` | Disconnect on deep nesting |
| NBT string length ≤ 32767 | `nbt.cpp` | Disconnect |

**No heap allocations in hot path** beyond pre-reserved `Buffer` vector.

### 3. HMAC Verification (Velocity MODERN)

- **Constant-time compare:** `CRYPTO_memcmp` (OpenSSL)
- **No early exit** on mismatch
- **Secret never logged**
- **Reject unknown versions** (1–4 only)

### 4. IPv6 Handling

- Single dual-stack socket (`IPV6_V6ONLY=0`)
- IPv4-mapped addresses normalized to dotted-quad for logging
- `%zone` IDs stripped from Velocity forwarded addresses
- Verified via `tests/integration/ipv6_status.py`

### 5. Memory Safety

| Practice | Enforcement |
|----------|-------------|
| No raw `new`/`delete` | `std::vector`, `std::string`, `std::unique_ptr` |
| No `memcpy` without bounds | `Buffer` read/write with `ensure()` checks |
| No `sprintf`/`strcpy` | `snprintf`, `std::format` (C++20) |
| Stack buffers sized by constant | `std::array`, `std::vector` |
| Integer overflow checks | `if (a > MAX - b) error` before `a + b` |

---

## Fuzz Harness

### Corpus

Located in `tests/fuzz/corpus/`:

| File | Tests |
|------|-------|
| `oversize_packet.bin` | Packet length > `max_packet_bytes` |
| `varint_overflow_*.bin` | VarInt 5+ bytes, negative lengths |
| `negative_length.bin` | String/array length = -1 (VarInt zigzag) |
| `bad_hmac*.bin` | Invalid HMAC, truncated, wrong msgId |
| `wrong_state_packets.bin` | Play packets in Login state, etc. |
| `flood.bin` | Rapid packets > rate limit |
| `slowloris.bin` | Header bytes trickled slowly |
| `truncated_login_start.bin` | Incomplete LoginStart |
| `huge_string_*.bin` | 100MB+ strings |

### Harness

```python
# tests/fuzz/fuzz_packet.py
# Feeds each corpus file to limbo via raw TCP
# Expects: clean disconnect (no crash, no sanitizer error)
# Runs under ASAN/UBSAN: make test-fuzz (planned)
```

### Running Fuzz Tests

```sh
# Build with sanitizers (planned Makefile target)
CXXFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" make clean all

# Run harness
python3 tests/fuzz/fuzz_packet.py --host 127.0.0.1 --port 25566

# Or with AFL/libFuzzer (future)
```

---

## Rate Limiting Details

### Sliding Window (Per Connection)

```cpp
// limits.cpp
struct RateLimiter {
    static constexpr int WINDOW_MS = 1000;
    static constexpr int BUCKETS = 1000;  // 1ms granularity

    std::array<uint64_t, BUCKETS> packets{};
    std::array<uint64_t, BUCKETS> bytes{};
    int head = 0;

    bool checkPacket() { return sumWindow(packets) < maxPacketsPerSec; }
    bool checkBytes(size_t n) { return sumWindow(bytes) + n < maxBytesPerSec; }

    void recordPacket() { packets[head]++; }
    void recordBytes(size_t n) { bytes[head] += n; }

    void advance(int nowMs) {
        int newHead = (nowMs / 1000) % BUCKETS;
        if (newHead != head) {
            // Zero buckets we're leaving behind
            for (int i = head + 1; i != newHead; i = (i + 1) % BUCKETS) {
                packets[i] = 0;
                bytes[i] = 0;
            }
            head = newHead;
        }
    }
};
```

### Tuning Guidelines

| Traffic Profile | `max_packets_per_sec` | `max_bytes_per_sec` |
|-----------------|----------------------|---------------------|
| Vanilla idle (keepalive) | 10 | 1 KB |
| Normal play | 50–100 | 50–100 KB |
| Aggressive bot | 200+ | 500 KB+ |
| **Default** | **100** | **1 MiB** |

**Monitor via logs:**
```
[limbo] peer=1.2.3.4:12345 rate limit exceeded packets=150 limit=100
[limbo] peer=1.2.3.4:12345 rate limit exceeded bytes=2097152 limit=1048576
```

---

## Integration Test Matrix

`tests/integration/matrix.py` tests:

| Category | Versions | Forwarding | Expect |
|----------|----------|------------|--------|
| Vanilla direct | 47, 340, 754, 762, 763, 764, 766, 767 | NONE | Join + stay 30s |
| Velocity MODERN | 754, 762, 763, 764, 766, 767 | MODERN | Join + stay 30s |
| Bad secret | 767 | MODERN (wrong) | Disconnect |
| Oversize packet | 767 | NONE | Disconnect |
| Flood | 767 | NONE | Disconnect (no crash) |
| IPv6 | 767 | NONE | Join over `::1` |
| Slowloris | 767 | NONE | Timeout disconnect |

**Acceptance gate:** All above pass + fuzz corpus 0 crashes.

---

## Soak Test

`tests/soak/soak.py`:

| Metric | Target |
|--------|--------|
| 1000 idle connections | RSS < 256 MB |
| Join latency (P99) | < 50 ms |
| CPU (idle) | < 5% single core |
| No FD leaks | `lsof -p <pid>` stable |
| No memory growth | 1 hour run, RSS flat |

```sh
# Run soak (requires limbo running)
python3 tests/soak/soak.py --host 127.0.0.1 --port 25566 --connections 1000 --duration 3600
```

---

## Security Checklist (Pre-Release)

- [ ] `make test` passes (unit + HMAC vector)
- [ ] `tests/integration/matrix.py --all` passes
- [ ] `tests/fuzz/fuzz_packet.py` 0 crashes (ASAN clean)
- [ ] `tests/soak/soak.py` 1k idle 1hr clean
- [ ] `forwarding=MODERN` + wrong secret → disconnect
- [ ] `forwarding=MODERN` + PVN < 393 → disconnect
- [ ] IPv6 `::` bind + v4 client → logged as dotted-quad
- [ ] `max_players` exceeded → immediate close, no fd leak
- [ ] `read_timeout_ms` → disconnect on idle
- [ ] Oversize packet → disconnect, no OOB read
- [ ] VarInt overflow → disconnect, no infinite loop
- [ ] Deep NBT nesting → disconnect, no stack overflow

---

## Incident Response

| Event | Detection | Response |
|-------|-----------|----------|
| Crash (segfault/abort) | Systemd/coredump | Restart via proxy health check; analyze core |
| Memory leak | RSS growth in soak | Restart; bisect with `heaptrack` |
| Rate limit spam | Log volume spike | Proxy-level IP ban; increase limits if false positive |
| HMAC failures | Log "Invalid forwarding signature" | Rotate secret; check Velocity config |

---

## Related

- [Configuration](Configuration) — all limit keys
- [Velocity Forwarding](Velocity-Forwarding) — HMAC details
- [Testing](Testing) — matrix, fuzz, soak
- [Architecture](Architecture) — thread model, limits rationale