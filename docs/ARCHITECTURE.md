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
- 2026-10-05 exact-protocol pass (lead): replaced era-approximate packet IDs with
  exact per-PVN tables verified against minecraft-data `protocol.json` exact-minor
  dirs + real-client proof. Found and fixed: 1.12 split (335 vs 338/340), 1.16
  split (735/736 vs 751+), 1.17-1.18 KA/center, per-minor 1.19 rows, 1.20.3+
  center/batch, 766-775 rows, serverbound KA per era, 775 cap (26.1 newest
  verified data). PVN ranges now enumerate releases only (authoritative map from
  minecraft-data `versionsByMinecraftVersion`: 1.14.2=485, 1.14.3=490,
  1.14.4=498, 1.15.1=575, 1.15.2=578 — an earlier edit wrongly used 574/481/482).
- Real registry codecs: `tools/gen_codec.py` converts minecraft-data
  `loginPacket.json` NBT to binary blobs (`src/protocol/codec_data.*`, 45 blobs):
  JoinGame codecs per era (filtered overworld+plains+chat+damage, vanilla IDs
  kept), 764/765 config compound, 766+ per-registry entries. NBT named vs
  anonymous shapes verified per field (JoinGame/dimension/chunk<=1.20.1 named;
  chunk 1.20.2+/disconnect-reason anonymous; login-disconnect always string).
  Fixed along the way (all caught by real `minecraft-protocol` clients):
  Login Success dashed-UUID pre-1.16 / properties 1.19+ / strictErrorHandling
  766-767-only, 1.8 VarInt KeepAlive, MOTION_BLOCKING length 13->15,
  1.16.2+ chunk shape (no ignoreOldData, counted biomes), config Settings-first
  tolerance with poll-based EOF safety, empty-batch bracketing 1.20.2+.
- Schematic: crew reader could not parse real files (VarInt-name dialect +
  fixed-Huffman-only inflate) — lead rewrote `third_party/miniz.c` (full
  inflate: stored/fixed/dynamic + zlib/gzip checks, byte-exact vs python zlib)
  and `src/world/schematic.cpp` (standard NBT, v2 + v3 nest, caps). Paste is
  shape-preserving stone/air on 1.18+ (stone global id 1 stable since 1.13);
  sections only span y -64..319 so the platform sits at y<=319 with spawn on
  top (void spawn stays y=400). Full per-block palette mapping deferred.
- Limits wired into every `readPacket` (lead): token buckets + frame caps.
- Evidence: `make test` 12 suites green, 0 warnings; matrix 41/41 PVNs;
  hardening 40/40; fuzz suites pass with liveness; soak 300/300 holders 60s,
  join p50 ~3ms, RSS 5.5MB base + ~33KB/conn (15.5MB peak; post-run 15.36MB —
  thread-stack return lag, no growth across repeated runs); real-client holds
  15s on 16 version labels; Velocity MODERN accept/reject re-verified.
- Known limits: full block palette mapping (paste is stone/air shapes);
  schematic render on <1.18 is void; PVN >775 clean-rejected (no data);
  1.21.5+ chunk heightmaps use new array shape (implemented); soak RSS
  "after" includes unreclaimed thread stacks (observe, not a leak signal).
