# Architecture hypotheses (may be redesigned on evidence)

Date: 2026-10-05. Initial hypotheses only — redesign when evidence shows better
correctness/compat/perf/memory/security/reliability/maintainability/speed/testing/extensibility.

## 1. Transport: thread-per-connection (TCP, blocking) + strict limits

- Why: simplest correct first; STL only (`<thread>`, `<sys/socket.h>`), no Boost.Asio/libuv vendoring.
- Limits make it safe: `max_players` cap, `read_timeout_ms` (default 30s), `max_packet_bytes=8192`,
  `max_packets_per_sec` guard in connection loop. Idle 1k threads is heavy but acceptable for v0.1;
  measure RSS/CPU with soak test before moving to poll/epoll.
- Alternative considered: epoll event-loop. Deferred until benchmark proves thread model fails scale target
  (1k idle, <256MB base). Evidence required: `tests/soak` RSS + join latency.
- No cmake: `Makefile` + `g++ -std=c++20` only, to work without sudo (cmake/zlib headers absent).
  No heavy deps: OpenSSL libcrypto only (already present) for Velocity HMAC-SHA256; zlib via vendored
  `miniz` later for `.schem` gzip (not yet needed for void-only).

## 2. Protocol: data-driven version tables, not hardcoded IDs

- `src/protocol/versions.h`: map protocol-version-number (PVN) -> era + packet IDs for
  Login/Configuration/Play (JoinGame, KeepAlive, Position, Chunk, Disconnect, etc.).
- Fill from PrismarineJS `minecraft-data` `protocol.json` per version + NanoLimbo `State.java` /
  PicoLimbo `packets.json` as cross-check. Logic branches on era (e.g. `<1.20.2` skip Configuration,
  `>=1.20.3` send GameEvent 13 before chunks), never on single magic ID.
- Wire primitives (`VarInt`, `String`, `UUID`, framing `len+id+body`) in `protocol/` with unit tests.
  NBT writer minimal (unnamed compound + string/int/long/byte-array) for RegistryData/Chunk heightmaps.

## 3. Login: offline + Velocity MODERN, skip encryption/compression behind proxy

- Behind Velocity: never send `EncryptionRequest`; omit `SetCompression` (leave uncompressed).
  Velocity terminates client crypto/compression; backend stays offline-mode.
- MODERN: after `LoginStart`, send `LoginPluginRequest{msgId, "velocity:player_info"}`,
  await `LoginPluginResponse`, HMAC-SHA256 verify with constant-time compare, accept versions 1..4,
  replace address/UUID/username. Failure -> Login `Disconnect`. Direct (NONE) -> offline UUID
  `UUIDv3("OfflinePlayer:"+name)`, no trust in spoofed headers.
- Legacy/BungeeGuard: deferred (only if acceptance demands `<1.13` via proxy without modern).

## 4. Play minimal: void + spectator + keepalive, ignore movement

- Spawn `8.5,400,8.5` void, dimension overworld, gamemode spectator(3), abilities fly,
  `viewDistance=2`, single empty chunk at 0,0 (air palette 0, plains biome, empty light),
  `GameEvent 13` for `>=1.20.3`, periodic `KeepAlive`, answer teleport-confirm, drop movement packets.
- Schematic (`.schem` Sponge v2 paste at spawn) is stage 2; void fallback always works.
  `.schematic`/`.litematic`/`.mca` deferred unless demanded.

## 5. What was deliberately NOT built

- No auth/encryption, no compression, no world ticking/physics, no commands/plugins,
  no Bedrock/Geyser handling (transparent at proxy), no Prometheus/metrics.
  Proxy provides those; backend stays minimal per core goal.

## 6. Change log

- 2026-10-05: initial hypotheses above. Next evidence gates: unit tests -> status/login live test ->
  single-version play join (767) -> version-table fill -> Velocity e2e -> soak/fuzz.
- 2026-10-05 IPv6: single `AF_INET6` listener with `IPV6_V6ONLY=0` (dual-stack; Context7
  Boost.Asio `ip::v6_only` + `ip::tcp::v6`/`acceptor` docs confirm semantics, applied to POSIX
  sockets in `src/server/tcp_server.cpp:14`). `bind` resolves via `getaddrinfo` (numeric v6/v4 +
  hostnames, `src/server/tcp_server.cpp:29`); v4-mapped peers logged as dotted-quad
  (`peerIp`); Velocity forwarding strips IPv6 `%zone` (`src/security/velocity.cpp:43`).
  Verified: `make test` + `ipv6_status.py` over `::1`, `status_ping.py` over `127.0.0.1`,
  bind `::1`/`127.0.0.1`/bad-host cases.
