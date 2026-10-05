# limbo-c++ — lightweight Minecraft Limbo backend (C++20)

Primary behind-proxy Limbo. Proxy owns auth/version translation where possible.
See **[Wiki](wiki/Home)** for full documentation, or `docs/research/limbo-references.md` for protocol sources and `docs/ARCHITECTURE.md` for decisions.

## Quick Start

Requires: `g++ >= 12`, `make`, OpenSSL libcrypto (headers + `.so.3`).

```sh
make            # builds build/limbo
make test       # builds + runs unit tests + HMAC vector check
./build/limbo --config config/limbo.properties
```

## Config

`config/limbo.properties` (key=value) — **[full reference](wiki/Configuration)**:

```properties
bind=0.0.0.0
port=25566
max_players=100
version_name=Limbo-C++
motd=Limbo-C++ void
protocol_max=774
forwarding=NONE|MODERN   # MODERN requires secret
forwarding_secret=
read_timeout_ms=30000
max_packet_bytes=8192
```

- `bind`: `0.0.0.0` or `::` (dual-stack IPv6), `127.0.0.1`, `::1`, or hostname.
  Single IPv6 socket with `IPV6_V6ONLY=0` accepts both families (v4 appears as
  `::ffff:a.b.c.d`, logged as dotted-quad). Verified over `::1` and `127.0.0.1`
  (`tests/integration/ipv6_status.py`).

## Status

**v0.1**: primitives + status ping + offline login + Velocity MODERN verify + void-play stub.
Multi-version tables are data-driven in `src/protocol/versions.h` — fill from `minecraft-data` protocol.json, do not hardcode single IDs in logic.

See **[Protocol Support](wiki/Protocol-Support)** for supported versions and **[Architecture](wiki/Architecture)** for design.

## Testing

- `make test`: C++ unit (`varint`, `buffer`, `registry`, `void_chunk`, `velocity`, `limits`, `nbt`) + Python HMAC vector.
- `tests/integration/status_ping.py`: raw-socket handshake/status/login against live server.
- `tests/integration/matrix.py`: full version matrix (1.8/1.12/1.16/1.19/1.20/1.21 via Velocity) — acceptance gate.
- `tests/fuzz/fuzz_packet.py`: malformed packet corpus — 0 crashes required.
- `tests/soak/soak.py`: 1k idle connections, 1hr — RSS < 256MB, P99 join < 50ms.

See **[Testing](wiki/Testing)** for full test guide.

## Documentation

| Topic | Link |
|-------|------|
| **User Guide** | [Getting Started](wiki/Getting-Started) • [Configuration](wiki/Configuration) • [Velocity Forwarding](wiki/Velocity-Forwarding) |
| **Technical** | [Architecture](wiki/Architecture) • [Protocol Support](wiki/Protocol-Support) • [Void World](wiki/Void-World) • [Schematic Support](wiki/Schematic-Support) |
| **Operations** | [Hardening & Security](wiki/Hardening-Security) • [Testing](wiki/Testing) • [Troubleshooting](wiki/Troubleshooting) |
| **Development** | [Development](wiki/Development) |

## License

MIT — see [LICENSE](LICENSE). Author and maintainer: **Paranthaman**
(<paranthaman@example.com>, [@Paranthaman-K6](https://github.com/Paranthaman-K6)).
