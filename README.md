# fastlimbo

Lightweight behind-proxy Minecraft Limbo backend in C++20.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Language: C++20](https://img.shields.io/badge/C%2B%2B-20-ff69b4.svg)](httpsis.fecw)
[![Protocol: 776](https://img.shields.io/badge/protocol-776_(26.2_only)-9cf)](https://wiki.vg/Protocol)
[![OpenSSLL: libcrypto](https://img.shields.io/badge/deps-OpenSSL%20libcrypto-9cf)](https://openssl.org)

## Quick start

```sh
make            # builds fastlimbo
make test       # builds + runs unit tests
./build/fastlimbo --config config/limbo.properties
```

## Features

- **26.2-only support** — single-version release line in `src/protocol/versions.h`, PVN 776
- **Status ping** + **offline login** (UUID offline-mode)
- **Velocity MODERN forwarding** with HMAC-SHA256 verification
- **Dual-stack IPv6** (bind `::`, accepts both v4 and v6)
- **Configuration state** (1.20.2+, registry data)
- **Void play burst** — join game, chunk, keepalive, abilities, game event
- **Sponge `.schem` v2** reader (zlib via bundled miniz)
- **Fuzz harness** — malformed packet corpus, 0 crashes required
- **Soak test** — 1k idle connections, RSS < 256MB, P99 join < 50ms

## Building

```sh
make            # -> build/fastlimbo
make test       # unit tests + HMAC vector check
```

## Documentation

- [Wiki](wiki/Home) — per-component status, configuration, protocol support
- [Protocol Support](wiki/Protocol-Support) — version eras, packet IDs
- [Architecture](wiki/Architecture) — design decisions, thread model
- [Hardening & Security](wiki/Hardening-Security) — rate limits, fuzzing, IPv6
- [Testing](wiki/Testing) — unit, integration, fuzz, soak, matrix

## License

**MIT** — see [LICENSE](LICENSE).

Copyright (c) 2026 Paranthaman ([@Paranthaman-K6](https://github.com/Paranthaman-K6)).

`third_party/miniz.c` is public domain (CC0). Protocol details derived from publicly documented behaviour and reference implementations; no third-party source was vendored.

---

## Contributing

See [CLA.md](CLA.md). Pull requests require a signed CLA. Code is released under MIT — contributions are welcomed but must retain the license header.

## Contact

Paranthaman — [@Paranthaman-K6](https://github.com/Paranthaman-K6)