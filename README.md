<h1 align="center">limbo-c++</h1>

<p align="center">
  <strong>Lightweight behind-proxy Minecraft Limbo backend in C++20</strong><br>
  <sub>Handshake&nbsp;→&nbsp;join&nbsp;→&nbsp;idle&nbsp;→&nbsp;clean&nbsp;kick. No world, no auth, no plugins.</sub>
</p>

<p align="center">
  <img alt="Proprietary" src="https://img.shields.io/badge/license-PROPRIETARY-ff4d4d?style=flat-square&labelColor=1e1e1e">
  <img alt="All rights reserved" src="https://img.shields.io/badge/all%20rights%20reserved-ff4d4d?style=flat-square&labelColor=1e1e1e">
  <img alt="No license" src="https://img.shields.io/badge/license-NONE-ff4d4d?style=flat-square&labelColor=1e1e1e">
  <a href="https://github.com/Paranthaman-K6/limbo/actions/workflows/docs.yml"><img alt="Docs" src="https://img.shields.io/badge/docs-live-00ffff?style=flat-square&labelColor=1e1e1e"></a>
  <img alt="Language" src="https://img.shields.io/badge/C%2B%2B-20-0066ff?style=flat-square&labelColor=1e1e1e">
  <img alt="PVN" src="https://img.shields.io/badge/protocol-47...774-0066ff?style=flat-square&labelColor=1e1e1e">
  <img alt="Deps" src="https://img.shields.io/badge/deps-OpenSSL%20libcrypto-0066ff?style=flat-square&labelColor=1e1e1e">
</p>

> [!WARNING]
> **This project is proprietary software with all rights reserved. It is not
> licensed under any open-source license, and no permission is granted to copy,
> modify, or redistribute it.** Every source file carries this notice, so it
> travels with any copy. Reading and learning from the code is welcome;
> redistributing or building on it is not. See [LICENSE](LICENSE) for terms and
> [CLA.md](CLA.md) to contribute.


<p align="center">
  <a href="https://Paranthaman-K6.github.io/limbo/"><strong>📖 Documentation</strong></a>
  &nbsp;·&nbsp;
  <a href="wiki/Home">Wiki source</a>
  &nbsp;·&nbsp;
  <a href="#quick-start">Quick start</a>
  &nbsp;·&nbsp;
  <a href="issues">Issues</a>
</p>

---

The **proxy owns everything expensive** — authentication, version translation,
encryption, compression, and metrics. `limbo-c++` only completes the minimal
`Login → Play` handshake so a vanilla client can join a void world, sit idle, and
be kicked cleanly. That division is the whole design.

| The proxy handles | limbo-c++ handles |
|-------------------|-------------------|
| Authentication (online/offline, Mojang session) | `Handshake` + `Status` ping |
| Version translation (so one backend spans many clients) | `Login Success` + offline-mode UUID |
| Encryption & compression | Minimal `Play` burst into a void world |
| Metrics, bans, permissions | `KeepAlive` + clean disconnect |

**Where it fits:** holding players during maintenance, queue servers, proxy-side
minigames, auth bridges, and protocol testing harnesses.

---

## Quick start

Requires `g++ >= 12`, `make`, and OpenSSL libcrypto (headers + `.so.3`).

```sh
git clone https://github.com/Paranthaman-K6/limbo.git
cd limbo
make                 # -> build/limbo
make test            # unit tests + HMAC vector check
./build/limbo --config config/limbo.properties
```

That's it. The server now answers pings on `0.0.0.0:25566`.

<details>
<summary>Verify it with a real client handshake</summary>

```sh
python3 tests/integration/status_ping.py          # raw-socket status + login
python3 tests/integration/ipv6_status.py          # dual-stack IPv6 bind check
python3 tests/integration/matrix.py              # full version matrix
```

See **[Testing](https://Paranthaman-K6.github.io/limbo/Testing/)** for the full guide.

</details>

---

## Configuration

`config/limbo.properties` — plain `key=value`. Full reference in
**[Configuration](https://Paranthaman-K6.github.io/limbo/Configuration/)**.

```properties
bind=0.0.0.0
port=25566
max_players=100
version_name=Limbo-C++
motd=Limbo-C++ void
protocol_max=774
forwarding=NONE|MODERN   # MODERN requires a secret
forwarding_secret=
read_timeout_ms=30000
max_packet_bytes=8192
```

<details>
<summary>Binding, including dual-stack IPv6</summary>

`bind` accepts `0.0.0.0`, `::` (dual-stack), `127.0.0.1`, `::1`, or a hostname.

Binding `::` creates a **single** IPv6 socket with `IPV6_V6ONLY=0`, so it accepts
both address families. IPv4 clients appear as `::ffff:a.b.c.d` and are logged as
dotted-quad. Verified over `::1` and `127.0.0.1` by
`tests/integration/ipv6_status.py`.

</details>

### Velocity forwarding

Run behind a Velocity proxy with `forwarding=MODERN` and a shared
`forwarding_secret`, and every connection is authenticated with HMAC-SHA256
before a single packet is trusted. See
**[Velocity Forwarding](https://Paranthaman-K6.github.io/limbo/Velocity-Forwarding/)**.

---

## Protocol support

Multi-version tables are **data-driven** in
[`src/protocol/versions.h`](src/protocol/versions.h) — eras map to packet-ID
tables, so adding a version means adding data, never editing logic.

| Era | Protocol versions | Notes |
|-----|-------------------|-------|
| 1.8.x | 47 | Legacy handshake, no Configuration state |
| 1.12.x | 335–340 | |
| 1.13 – 1.15 | 393–578 | Login plugin request era |
| 1.16 – 1.18 | 735–758 | |
| 1.19 | 759–762 | Signed properties on Login Success |
| 1.20.2 | 764 | **Configuration state** introduced |
| 1.20.3 – 1.20.6 | 765–766 | Registry Data split, NBT text components |
| 1.21.x | 767–774 | GameEvent 13, teleport-id-first position |

See **[Protocol Support](https://Paranthaman-K6.github.io/limbo/Protocol-Support/)**
for per-era packet IDs and registry layout.

---

## Architecture

```
proxy (auth, encryption, versions)
        │
        ▼
   TCP listener ──► per-connection thread ──► state machine
                                                  │
                        ┌─────────────────────────┼──────────────┐
                    Handshake              Login            Play
                        │                     │                 │
                    Status ping        LoginSuccess       JoinGame + void
                                    (+ HMAC verify)      chunk + KeepAlive
```

- **Threading** — thread-per-connection, detached; no shared world state.
- **Safety** — every read is bounds-checked, every string length is capped, and
  every VarInt decode rejects over-long encodings.
- **No zlib dependency** — a small public-domain inflate lives in
  `third_party/miniz.c` for `.schem` reading.

Deeper write-ups in **[Architecture](https://Paranthaman-K6.github.io/limbo/Architecture/)**
and `docs/ARCHITECTURE.md`.

---

## Testing

```sh
make test
```

| Suite | What it proves |
|-------|----------------|
| `tests/test_*.cpp` | Unit: varint, buffer, nbt, registry, void_chunk, velocity, limits, miniz |
| `tests/integration/status_ping.py` | Raw-socket handshake → status → login against a live server |
| `tests/integration/ipv6_status.py` | Dual-stack IPv6 bind accepts v4 and v6 |
| `tests/integration/matrix.py` | **Acceptance gate** — 1.8 / 1.12 / 1.16 / 1.19 / 1.20 / 1.21 via Velocity |
| `tests/fuzz/fuzz_packet.py` | Malformed-packet corpus — **0 crashes** required |
| `tests/soak/soak.py` | 1k idle connections for 1hr — RSS < 256MB, P99 join < 50ms |

**[Testing guide](https://Paranthaman-K6.github.io/limbo/Testing/)**

---

## Status

**v0.1** — primitives, status ping, offline login, Velocity MODERN verification,
and the void-play burst are done and tested. Schematic paste, rate limiting, and
the full version matrix are in progress.

The live per-component status table lives in
**[wiki/Home](https://Paranthaman-K6.github.io/limbo/)**.

---

## Documentation

Full docs are published to **`https://Paranthaman-K6.github.io/limbo/`** and
built from [`wiki/`](wiki) by MkDocs — edit a page and CI republishes it.

| | |
|---|---|
| **User guide** | [Getting Started](https://Paranthaman-K6.github.io/limbo/Getting-Started/) · [Configuration](https://Paranthaman-K6.github.io/limbo/Configuration/) · [Velocity Forwarding](https://Paranthaman-K6.github.io/limbo/Velocity-Forwarding/) |
| **Technical** | [Architecture](https://Paranthaman-K6.github.io/limbo/Architecture/) · [Protocol Support](https://Paranthaman-K6.github.io/limbo/Protocol-Support/) · [Void World](https://Paranthaman-K6.github.io/limbo/Void-World/) · [Schematic Support](https://Paranthaman-K6.github.io/limbo/Schematic-Support/) |
| **Operations** | [Hardening & Security](https://Paranthaman-K6.github.io/limbo/Hardening-Security/) · [Testing](https://Paranthaman-K6.github.io/limbo/Testing/) · [Troubleshooting](https://Paranthaman-K6.github.io/limbo/Troubleshooting/) |
| **Development** | [Development](https://Paranthaman-K6.github.io/limbo/Development/) |

<details>
<summary>Running the docs locally</summary>

```sh
python3 -m venv .venv-docs && source .venv-docs/bin/activate
pip install -r requirements-docs.txt
mkdocs serve      # http://127.0.0.1:8000/limbo/
```

</details>

---

## License

**Proprietary — all rights reserved.** Not open source. See
[LICENSE](LICENSE) and [NOTICE](NOTICE).

No permission is granted to copy, modify, or redistribute this project. If you
want to use it, contribute, or license it, ask first — see
[CLA.md](CLA.md) for the contribution terms.

Copyright © 2026 Paranthaman ([@Paranthaman-K6](https://github.com/Paranthaman-K6)).
"limbo-c++" and the project logo are trademarks of the copyright holder.

`third_party/miniz.c` is public domain (CC0) and separately licensed.
Protocol details were derived from publicly documented behaviour; no
third-party source was vendored.
