# Architecture

> **Source of truth:** [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md) — this page is a user-friendly digest.

---

## Design Principles

| Principle | Rationale |
|-----------|-----------|
| **Simplest correct first** | Thread-per-connection + blocking I/O; no event-loop until benchmarks prove it necessary |
| **Data-driven protocol** | Packet IDs from `minecraft-data` `protocol.json`; logic branches on *era*, never hardcoded IDs |
| **Proxy owns complexity** | No auth, encryption, compression, world, plugins — Velocity handles those |
| **No heavy deps** | STL + OpenSSL libcrypto only; `miniz` vendored for `.schem` gzip |
| **No CMake** | `Makefile` + `g++ -std=c++20` works without sudo on minimal containers |

---

## High-Level Flow

```
┌─────────────┐     ┌──────────────┐     ┌─────────────────┐
│  TCP Server │────▶│  Connection  │────▶│  Protocol State │
│  (accept)   │     │  (per-fd)    │     │  Machine        │
└─────────────┘     └──────────────┘     └────────┬────────┘
                                                   │
                        ┌──────────────────────────┼──────────────────────────┐
                        ▼                          ▼                          ▼
                 ┌─────────────┐            ┌─────────────┐            ┌─────────────┐
                 │  Handshake  │            │   Login     │            │   Play      │
                 │  (PVN +     │            │  (offline/  │            │  (void +    │
                 │   nextState)│            │   MODERN)   │            │   keepalive)│
                 └─────────────┘            └─────────────┘            └─────────────┘
                        │                          │                          │
                        ▼                          ▼                          ▼
                 ┌─────────────┐            ┌─────────────┐            ┌─────────────┐
                 │  Status     │            │ Configuration│            │  World      │
                 │  (ping)     │            │  (1.20.2+)  │            │  (void/     │
                 └─────────────┘            └─────────────┘            │  schematic) │
                                                                        └─────────────┘
```

---

## Thread Model

| Aspect | Detail |
|--------|--------|
| **Model** | Thread-per-connection (`std::thread` per accepted fd) |
| **I/O** | Blocking `read`/`write` with `SO_RCVTIMEO` / `SO_SNDTIMEO` |
| **Max threads** | `max_players` (default 100) + 1 listener |
| **Memory/thread** | ~1–2 MiB stack (default) + protocol buffers |
| **Scaling target** | 1,000 idle connections < 256 MB RSS (soak test pending) |
| **Alternative** | `epoll`/`io_uring` deferred until thread model fails target |

**Why not async?** For a Limbo (idle spectators), thread-per-connection is simpler, debuggable, and fast enough. Async adds complexity (state machines, lifetimes, cancellation) with no benefit until proven otherwise.

---

## Protocol: Data-Driven Version Tables

All version logic lives in [`src/protocol/versions.h`](../src/protocol/versions.h).

### Era System

| Era | PVN Range | Key Features |
|-----|-----------|--------------|
| `LEGACY_1_8` | 47 | Pre-1.13, no login plugin, legacy chunk |
| `V1_12` | 335–340 | 1.12.2, Flattening prep |
| `V1_13_1_15` | 393–578 | 1.13+, login plugin available |
| `V1_16` | 735–754 | 1.16.x, new chunk format |
| `V1_17_1_18` | 755–758 | 1.17–1.18, 3D biomes, new height |
| `V1_19` | 759–762 | 1.19.x, chat signing |
| `V1_20_0_1` | 763 | 1.20/1.20.1, no Configuration yet |
| `V1_20_2` | 764 | **Configuration state introduced** |
| `V1_20_3_4` | 765 | **GameEvent 13 required** |
| `V1_20_5_6` | 766 | 1.20.5/1.20.6 |
| `V1_21_0_1` | 767 | 1.21/1.21.1 |
| `V1_21_2_3` | 768–769 | 1.21.2/1.21.3 |
| `V1_21_4_PLUS` | 770+ | 1.21.4+ |

### Era Helpers (Branch on These, Not PVN)

```cpp
inline bool hasConfiguration(int pvn) { return pvn >= 764; }
inline bool hasLoginPlugin(int pvn)   { return pvn >= 393; }  // 1.13+
inline bool needsGameEvent13(int pvn) { return pvn >= 765; }  // 1.20.3+
```

### Packet ID Tables

`PlayIds playIds(int pvn)` returns era-correct IDs for:
- `joinGame`, `keepAlive`, `position`, `chunk`, `disconnect`
- `abilities`, `gameEvent`, `centerChunk`

**Never hardcode packet IDs in logic.** Always call `playIds(pvn)` or `playKeepAliveServerbound(pvn)`.

### Adding a New Version

1. Check `minecraft-data` `protocol.json` for the new PVN
2. Add to `eraForPvn()` range if new era, or extend existing
3. Add `PlayIds` entry in `playIds()` switch
4. Add `playKeepAliveServerbound()` entry
5. Add `versionName()` entry
6. Run `tests/integration/matrix.py` with new version

---

## State Machine

### Handshake (State 0)

| Dir | Packet | Notes |
|-----|--------|-------|
| C→S | `Handshake` | `protocol_version`, `address`, `port`, `next_state` (1=status, 2=login) |

### Status (State 1)

| Dir | Packet | Notes |
|-----|--------|-------|
| C→S | `Status Request` | |
| S→C | `Status Response` | JSON: version, players, description, favicon |
| C→S | `Ping Request` | payload: int64 |
| S→C | `Ping Response` | echo payload |

### Login (State 2) — Pre-1.20.2

| Dir | Packet | Notes |
|-----|--------|-------|
| C→S | `Login Start` | username (+ UUID if offline, SigData if 1.19+) |
| S→C | `Login Plugin Request` | `velocity:player_info` (if MODERN + PVN≥393) |
| C→S | `Login Plugin Response` | HMAC verification |
| S→C | `Login Success` | UUID + username (+ properties 1.19+) |
| — | **→ Play** | Immediate switch, no ack |

### Login (State 2) — 1.20.2+ (Configuration Era)

| Dir | Packet | Notes |
|-----|--------|-------|
| C→S | `Login Start` | |
| S→C | `Login Plugin Request` | `velocity:player_info` |
| C→S | `Login Plugin Response` | HMAC verify |
| S→C | `Login Success` | |
| C→S | `Login Acknowledged` | Switches to Configuration |
| S→C | `Plugin Message (brand)` | `minecraft:brand` |
| S→C | `Registry Data` | `dimension_type`, `biome`, `chat_type` (minimal) |
| S→C | `Finish Configuration` | |
| C→S | `Finish Configuration` (ack) | → Play |

### Play (State 3) — Minimal Burst

| Order | Packet | Era Notes |
|-------|--------|-----------|
| 1 | `Join Game` | dimension, gamemode=3, spawn pos, view_distance |
| 2 | `Plugin Message (brand)` | `minecraft:brand` = "Limbo-C++" |
| 3 | `Player Abilities` | flying, invulnerable, creative (spectator) |
| 4 | `Player Position and Look` | spawn X/Y/Z, yaw/pitch=0, flags=0 |
| 5 | `Game Event 13` | **Required PVN≥765** (1.20.3+) |
| 6 | `Chunk Data` | Single void chunk at (0,0) per era format |
| 7 | `Keep Alive` | Periodic (10s), kick on timeout; answer client echo |

**Movement packets from client are silently dropped.**

---

## Key Modules

| Module | Responsibility | Ownership |
|--------|----------------|-----------|
| `tcp_server` | `socket()`, `bind`, `listen`, `accept` loop, dual-stack | Lead |
| `connection` | Per-fd state machine, rate limits, read/write, protocol dispatch | Lead |
| `buffer` | Read/write cursor, `VarInt`, `String`, `UUID`, framing | Shared |
| `varint` | ZigZag, unsigned/signed VarInt encode/decode | Shared |
| `packets` | Packet builders for each state | Shared |
| `versions` | Era + packet ID tables (data-driven) | Shared |
| `nbt` | Minimal unnamed compound writer | Registry agent |
| `registry` | Configuration `RegistryData` + `Finish` | Registry agent |
| `play` | Play burst builders (JoinGame, chunk, etc.) | Play agent |
| `void_chunk` | Era-correct empty chunk encoder | Play agent |
| `schematic` | `.schem` v2 reader (miniz) | Schematic agent |
| `velocity` | MODERN HMAC-SHA256 verify (constant-time) | Security agent |
| `limits` | Rate limits (pps, Bps), max_packet_bytes | Hardening agent |

---

## Security Boundaries

```
┌─────────────────────────────────────────────────────────────┐
│                     VELOCITY PROXY                          │
│  TLS • Auth • Version Translation • Compression • Metrics   │
└──────────────────────────┬──────────────────────────────────┘
                           │ TCP (trusted LAN / localhost)
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                      LIMBO-C++                              │
│  • Offline UUID / Velocity MODERN HMAC                      │
│  • No encryption (proxy terminates)                         │
│  • No compression (proxy terminates)                        │
│  • Rate limits: pps, Bps, max_packet, read_timeout          │
│  • Malformed packet → disconnect (no crash)                 │
│  • Void world only (no disk, no tick)                       │
└─────────────────────────────────────────────────────────────┘
```

**Threat model:** Malformed/malicious packets from proxy (buggy proxy, compromised proxy, or direct attack if exposed). Defense: strict limits, fuzz-tested parsers, constant-time HMAC, no heap allocations in hot path beyond buffers.

---

## Memory & Performance

| Metric | Target | Measurement |
|--------|--------|-------------|
| Idle RSS (1k connections) | < 256 MB | `tests/soak/soak.py` |
| Join latency (P99) | < 50 ms | `tests/soak/soak.py` |
| CPU (idle 1k) | < 5% core | `top` / `perf` |
| Packet parse (hot path) | 0 allocs | `buffer` reuses `std::vector` |

**No allocations in steady state:** `Buffer` reserves once, `void_chunk` encodes to stack/preallocated buffer, `KeepAlive` reuses single packet.

---

## Build System

```make
# Makefile — single file, no cmake
CXX = g++
CXXFLAGS = -std=c++20 -O2 -pipe -Wall -Wextra -Wpedantic \
           -I. -Isrc -Ithird_party
LDFLAGS = -lcrypto -lpthread

# Targets: limbo, test_*, clean, fmt
```

- `-I.` enables `#include "config.h"` from `src/`
- `-Ithird_party` for `miniz.h`
- OpenSSL `libcrypto` only for HMAC-SHA256
- `pthread` for `std::thread`

---

## Extending the Protocol

### Add a New Play Packet

1. Add ID to `PlayIds` in `versions.h` for relevant eras
2. Add builder in `play.h/.cpp` (or `packets.h/.cpp`)
3. Call from `connection.cpp` play burst (lead merge only)
4. Add unit test in `tests/test_play.cpp`
5. Add integration check in `matrix.py`

### Add a New Era

1. Add enum value in `Era`
2. Extend `eraForPvn()` ranges
3. Add `PlayIds` entry
4. Add `playKeepAliveServerbound()` entry
5. Add `versionName()` entry
6. Implement void chunk format in `void_chunk.cpp` if different
7. Run matrix test

---

## Related Docs

- [Protocol Support](Protocol-Support) — detailed packet tables
- [Velocity Forwarding](Velocity-Forwarding) — HMAC flow
- [Void World](Void-World) — chunk formats per era
- [Hardening & Security](Hardening-Security) — limits, fuzzing
- [Development](Development) — contributing, code style