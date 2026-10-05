# Testing

> **Run all:** `make test` (C++ units + HMAC vector) + integration matrix + fuzz + soak

---

## Test Pyramid

```
                    ┌─────────────────┐
                    │   Soak Test     │  1k idle, 1hr, RSS/latency
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
       ┌────────────┐ ┌────────────┐ ┌────────────┐
       │ Integration│ │   Fuzz     │ │  Matrix    │
       │  (Python)  │ │  (corpus)  │ │ (versions) │
       └─────┬──────┘ └─────┬──────┘ └─────┬──────┘
             │              │              │
             └──────────────┼──────────────┘
                            ▼
                   ┌─────────────────┐
                   │  C++ Unit Tests │  varint, buffer, registry, void_chunk,
                   │  (Catch2-like)  │  velocity, limits, nbt, schematic, play
                   └─────────────────┘
```

---

## 1. C++ Unit Tests (`make test`)

### Test Binaries

| Binary | Module | Coverage |
|--------|--------|----------|
| `build/test_varint` | `varint.h` | ZigZag, unsigned/signed encode/decode, edge cases |
| `build/test_buffer` | `buffer.h/.cpp` | Read/write cursor, VarInt, String, UUID, framing |
| `build/test_registry` | `registry.h/.cpp` | NBT writer, RegistryData, Finish Configuration |
| `build/test_void_chunk` | `void_chunk.h/.cpp` | Era-correct void chunk encoding |
| `build/test_velocity` | `velocity.h/.cpp` | HMAC-SHA256 verify, payload parse, constant-time |
| `build/test_limits` | `limits.h/.cpp` | Rate limiters, sliding window, edge cases |
| `build/test_nbt` | `nbt.h/.cpp` | Compound, string, int, long, byte array |
| `build/test_schematic` | `schematic.h/.cpp` | `.schem` v2 parse (when ready) |
| `build/test_play` | `play.h/.cpp` | Play burst builders (when ready) |

### Running

```sh
make test
# Output:
# [test_varint] ...... PASS
# [test_buffer] ...... PASS
# [test_registry] .... PASS
# [test_void_chunk] .. PASS
# [test_velocity] .... PASS (includes HMAC vector)
# [test_limits] ...... PASS
# [test_nbt] ......... PASS
# [test_schematic] ... SKIP (not built yet)
# [test_play] ........ SKIP (not built yet)
# All tests passed.
```

### Adding a Unit Test

```cpp
// tests/test_newfeature.cpp
#include <cassert>
#include <cstdio>
#include "protocol/newfeature.h"

int main() {
    // Test case
    assert(limbo::proto::newFeature(42) == 84);
    printf("test_newfeature: PASS\n");
    return 0;
}
```

```make
# Makefile addition
test_newfeature: tests/test_newfeature.cpp src/protocol/newfeature.cpp ...
	$(CXX) $(CXXFLAGS) -o build/test_newfeature $^ $(LDFLAGS)

test: ... test_newfeature
	./build/test_newfeature
```

---

## 2. HMAC Vector Test (Python)

```sh
# Part of `make test`
python3 -c "
import hmac, hashlib
secret = bytes.fromhex('a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef')
payload = b'\x03\x07127.0.0.1' + bytes(16) + b'\x05Notch\x00'
expected = hmac.new(secret, payload, hashlib.sha256).digest()
print('HMAC vector:', expected.hex())
"
```

Verifies C++ `velocity.cpp` HMAC matches Python reference.

---

## 3. Integration Tests (`tests/integration/`)

### `status_ping.py`

```sh
python3 tests/integration/status_ping.py --host 127.0.0.1 --port 25566
# → Sends Handshake(next=1), Status Request, Ping Request
# → Validates JSON response, ping echo
```

### `ipv6_status.py`

```sh
python3 tests/integration/ipv6_status.py --host ::1 --port 25566
# → Same over IPv6 (dual-stack socket)
```

### `matrix.py` — **Primary Acceptance Gate**

```sh
# Single version, direct
python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 --version 767

# Single version, Velocity MODERN
python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 \
    --version 767 --forwarding modern --secret <hex>

# All supported versions, direct
python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 --all

# All versions, Velocity MODERN
python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 \
    --all --forwarding modern --secret <hex>

# With schematic (when ready)
python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 \
    --version 767 --schematic /data/lobby.schem
```

**What it does per version:**
1. Handshake (PVN)
2. Login Start (username `MatrixTest_<PVN>`)
3. If MODERN: Plugin Request/Response with HMAC
4. Login Success → [Configuration if PVN≥764] → Play
5. Verify Play burst: JoinGame, Abilities, Position, GameEvent13 (if PVN≥765), Chunk, KeepAlive
6. Send KeepAlive echo for 30s
7. Send movement packets (ignored)
8. Clean disconnect

**Exit codes:**
- `0` = all versions passed
- `1` = any version failed
- `2` = usage error

### `hardening_matrix.py`

```sh
python3 tests/integration/hardening_matrix.py --host 127.0.0.1 --port 25566
# Tests: oversize, flood, bad HMAC, slowloris, truncation, wrong state
```

---

## 4. Fuzz Tests (`tests/fuzz/`)

### Corpus

Pre-generated malformed packets in `tests/fuzz/corpus/` (see [Hardening & Security](Hardening-Security.md#fuzz-harness)).

### Harness

```sh
# Build with sanitizers (recommended)
CXXFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" make clean all

# Run
python3 tests/fuzz/fuzz_packet.py --host 127.0.0.1 --port 25566 --corpus tests/fuzz/corpus

# Expected: each corpus file → clean disconnect, no ASAN/UBSAN error
```

### Adding Corpus

```sh
# Capture a problematic packet (e.g., from Wireshark or proxy log)
# Save as tests/fuzz/corpus/new_case.bin
# Add to fuzz_packet.py corpus list
```

---

## 5. Soak Test (`tests/soak/`)

```sh
python3 tests/soak/soak.py \
    --host 127.0.0.1 --port 25566 \
    --connections 1000 \
    --duration 3600 \
    --ramp-up 60 \
    --join-rate 50
```

### Metrics Collected

| Metric | Target |
|--------|--------|
| RSS (1k idle) | < 256 MB |
| Join latency P50 | < 20 ms |
| Join latency P99 | < 50 ms |
| CPU (idle) | < 5% |
| FD count | Stable (no leak) |
| Disconnects (unexpected) | 0 |

### Output

```
[soak] Starting 1000 connections over 60s...
[soak] Ramp-up complete. 1000 connected.
[soak] 300s: RSS=184MB, joins=1000, p50=12ms, p99=38ms, cpu=2.1%
[soak] 600s: RSS=186MB, joins=1000, p50=11ms, p99=35ms, cpu=1.9%
[soak] 3600s: RSS=188MB, joins=1000, p50=11ms, p99=34ms, cpu=1.8%
[soak] PASS: all metrics within targets
```

---

## 6. Continuous Integration (Planned)

### GitHub Actions Workflow (`.github/workflows/ci.yml`)

```yaml
name: CI
on: [push, pull_request]
jobs:
  build-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get update && sudo apt-get install -y g++-12 make libssl-dev python3
      - name: Build
        run: make
      - name: Unit tests
        run: make test
      - name: Start limbo
        run: ./build/limbo --config config/limbo.properties &
      - name: Integration matrix
        run: python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 --all
      - name: Hardening matrix
        run: python3 tests/integration/hardening_matrix.py --host 127.0.0.1 --port 25566
      - name: Fuzz (ASAN)
        run: |
          CXXFLAGS="-fsanitize=address,undefined" make clean all
          python3 tests/fuzz/fuzz_packet.py --host 127.0.0.1 --port 25566
```

---

## Test Checklist (Pre-Merge)

- [ ] `make test` → all C++ units + HMAC vector PASS
- [ ] `matrix.py --all` (direct) → all versions join+stay 30s
- [ ] `matrix.py --all --forwarding modern` → all versions join+stay 30s
- [ ] `hardening_matrix.py` → all attack vectors disconnect cleanly
- [ ] `fuzz_packet.py` (ASAN) → 0 crashes, 0 sanitizer errors
- [ ] `soak.py` 1k/1hr → RSS < 256MB, P99 < 50ms, no leaks
- [ ] IPv6 `::1` status ping → PASS
- [ ] Schematic paste (when ready) → PASS

---

## Debugging Test Failures

### Verbose Output

```sh
# Unit tests
./build/test_buffer 2>&1 | head -50

# Integration
python3 tests/integration/matrix.py -v --host 127.0.0.1 --port 25566 --version 767

# With proxy logs
# Velocity: tail -f logs/latest.log | grep -i forward
```

### Common Failure Patterns

| Symptom | Likely Cause |
|---------|--------------|
| "LoginSuccess not received" | Packet ID mismatch in `versions.h` |
| "Configuration stall" | Missing `RegistryData` or `Finish Configuration` |
| "Loading terrain..." hang | Missing `GameEvent 13` (PVN ≥ 765) |
| "Invalid chunk data" | Biome palette missing / wrong era format |
| HMAC fail | Secret mismatch or payload parse bug |
| Rate limit false positive | `max_packets_per_sec` too low for test burst |

---

## Related

- [Getting Started](Getting-Started.md) — quick test commands
- [Protocol Support](Protocol-Support.md) — version matrix
- [Hardening & Security](Hardening-Security.md) — fuzz corpus, rate limits
- [Velocity Forwarding](Velocity-Forwarding.md) — MODERN test flags