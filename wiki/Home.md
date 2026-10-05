# limbo-c++ Wiki

**limbo-c++** is a lightweight, behind-proxy Minecraft "Limbo" backend written in C++20. It handles the minimal Login → Play handshake so a vanilla client can join, stay idle, and be cleanly kicked — without world simulation, auth, encryption, or plugins. The proxy (Velocity, BungeeCord, Waterfall) owns authentication, version translation, encryption, compression, and metrics.

---

## Quick Links

| Topic | Description |
|-------|-------------|
| [Getting Started](Getting-Started.md) | Build, configure, and run in 3 commands |
| [Configuration](Configuration.md) | All `limbo.properties` keys explained |
| [Architecture](Architecture.md) | Design decisions, data-driven protocol, thread model |
| [Protocol Support](Protocol-Support.md) | Versions, eras, packet IDs, Configuration state |
| [Velocity Forwarding](Velocity-Forwarding.md) | MODERN forwarding setup and HMAC verification |
| [Void World](Void-World.md) | Spawn, chunks, keepalive, spectator mode |
| [Schematic Support](Schematic-Support.md) | Sponge `.schem` v2 paste at spawn (planned) |
| [Hardening & Security](Hardening-Security.md) | Rate limits, fuzz harness, IPv6, read timeouts |
| [Testing](Testing.md) | Unit, integration, fuzz, soak, matrix |
| [Development](Development.md) | Code layout, contributing, multi-agent workflow |
| [Troubleshooting](Troubleshooting.md) | Common issues and diagnostics |

---

## What Is a "Limbo"?

In Minecraft proxy terminology, a **Limbo** is a minimal backend server that:
- Accepts connections forwarded from a proxy (Velocity, BungeeCord, Waterfall)
- Completes the Login handshake (offline-mode or Velocity MODERN forwarding)
- Sends a minimal Play burst: `JoinGame` → void chunk → `KeepAlive` loop
- Ignores player movement/interaction (spectator mode at Y=400 in void)
- Never simulates a world, ticks entities, or loads chunks from disk

**Use cases:** holding players during maintenance, queue servers, proxy-side minigames, authentication bridges, testing harnesses.

---

## Project Status

| Component | Status |
|-----------|--------|
| Core primitives (VarInt, String, UUID, framing) | ✅ Done + tests |
| Status ping (handshake + status) | ✅ Done |
| Offline login + `LoginSuccess` | ✅ Done |
| Velocity MODERN forwarding (HMAC-SHA256) | ✅ Done + vectors |
| Dual-stack IPv6 (bind `::`, v4-mapped normalization) | ✅ Done |
| Configuration state (1.20.2+) + Registry Data | ✅ Done |
| Void play burst (JoinGame, Abilities, Position, GameEvent13, chunk, KeepAlive) | ✅ Done |
| Sponge `.schem` v2 reader (zlib via miniz) | 🟡 In progress |
| Rate limits / packet/sec / bytes/sec / max_players | 🟡 In progress |
| Fuzz harness (malformed packets) | 🟡 In progress |
| Version matrix (47, 340, 754, 762, 763, 764, 766, 767) | 🟡 In progress |
| Soak test (1k idle, join latency) | 🔲 Planned |

Progress is tracked on an internal task board; the per-component state table above is the public summary.

---

## License

!!! warning "Proprietary — all rights reserved"
    **This project is not open source.** No permission is granted to copy,
    modify, or redistribute it, in whole or in part. Every source file carries
    a copyright notice, so the reservation travels with any copy.

    Reading and learning from this project is welcome. Copying,
    redistributing, or building on it is not — ask first.

    Full terms in [`LICENSE`](https://github.com/Paranthaman-K6/limbo/blob/main/LICENSE)
    and [`CLA.md`](https://github.com/Paranthaman-K6/limbo/blob/main/CLA.md).

    Copyright © 2026 Paranthaman
    ([@Paranthaman-K6](https://github.com/Paranthaman-K6)). "limbo-c++" and the
    project logo are trademarks of the copyright holder.

    `third_party/miniz.c` is public domain (CC0) and separately licensed.

Copyright © 2026 Paranthaman. All rights reserved.
