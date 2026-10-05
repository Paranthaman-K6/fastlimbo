# Getting Started

## Prerequisites

| Tool | Minimum Version | Notes |
|------|-----------------|-------|
| `g++` | 12 (C++20) | Tested with 12, 13, 14 |
| `make` | Any | GNU Make |
| OpenSSL `libcrypto` | 3.x (`.so.3`) | Headers + shared lib; distro package `libssl-dev` / `openssl-devel` |
| Python 3 | 3.10+ | For integration tests & fuzz harness |

**No sudo, no CMake, no Boost.Asio, no zlib dev package** — `miniz` is vendored for `.schem` gzip.

---

## Build

```sh
git clone https://github.com/your-org/limbo-cpp.git
cd limbo-cpp
make            # → build/limbo
make test       # → unit tests + HMAC vector check
```

### Build Outputs

| Binary | Purpose |
|--------|---------|
| `build/limbo` | Main server |
| `build/test_*` | Unit test runners (varint, buffer, registry, void_chunk, velocity, limits, nbt, schematic, play) |

### Make Targets

| Target | Description |
|--------|-------------|
| `make` / `make all` | Build `build/limbo` |
| `make test` | Build + run all C++ unit tests + Python HMAC vector |
| `make clean` | Remove `build/` |
| `make fmt` | Format with `clang-format` (if installed) |

---

## Run

```sh
# Default config at config/limbo.properties
./build/limbo --config config/limbo.properties
```

### Command-Line Options

| Flag | Description |
|------|-------------|
| `-c`, `--config <path>` | Path to properties file (default: `config/limbo.properties`) |
| `-h`, `--help` | Show usage (not yet implemented) |

### Expected Output

```
[limbo] bind=0.0.0.0 port=25566 max_players=100 version=Limbo-C++ proto_max=774 forwarding=NONE
[limbo] listening on 0.0.0.0:25566 (fd=3)
[limbo] peer=127.0.0.1:12345 handshake pvn=767 state=1
[limbo] peer=127.0.0.1:12345 login start username=Notch
[limbo] peer=127.0.0.1:12345 login success uuid=offline-... username=Notch
[limbo] peer=127.0.0.1:12345 play burst done (join_game, abilities, pos, game_event13, chunk, keepalive)
[limbo] peer=127.0.0.1:12345 keepalive echo ok
```

---

## Quick Test (No Proxy)

```sh
# Terminal 1: start limbo
./build/limbo --config config/limbo.properties

# Terminal 2: status ping (Python)
python3 tests/integration/status_ping.py --host 127.0.0.1 --port 25566

# Terminal 3: full login + play join (Python)
python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 --version 767
```

---

## With Velocity (MODERN Forwarding)

### Velocity `velocity.toml`

```toml
[servers]
limbo = "127.0.0.1:25566"

[player-information-forwarding]
mode = "MODERN"
secret = "your-32-byte-hex-secret-here"  # 64 hex chars = 32 bytes
```

### limbo.properties

```properties
bind=0.0.0.0
port=25566
max_players=100
version_name=Limbo-C++
motd=Limbo-C++ void
protocol_max=774
forwarding=MODERN
forwarding_secret=your-32-byte-hex-secret-here
read_timeout_ms=30000
max_packet_bytes=8192
```

**The secret must match exactly** (64 hex characters). Generate one:

```sh
openssl rand -hex 32
```

---

## Directory Layout

```
limbo-cpp/
├── build/                 # Build artifacts (gitignored)
├── config/
│   └── limbo.properties   # Main configuration
├── docs/
│   ├── ARCHITECTURE.md    # Architecture decisions
│   ├── hardening-notes.md # Security hardening notes
│   ├── play-notes.md      # Play state packet notes
│   ├── schematic-notes.md # .schem format notes
│   └── research/
│       └── limbo-references.md  # Consolidated protocol references
├── src/
│   ├── main.cpp                 # Entry point
│   ├── config.h/.cpp            # Config loading
│   ├── server/
│   │   ├── tcp_server.h/.cpp    # Listener + accept loop
│   │   └── connection.h/.cpp    # Per-connection handler (lead-owned)
│   ├── protocol/
│   │   ├── buffer.h/.cpp        # Read/write cursor + VarInt/String/UUID
│   │   ├── varint.h             # ZigZag + VarInt helpers
│   │   ├── packets.h/.cpp       # Packet builders (Login/Config/Play)
│   │   ├── versions.h           # Data-driven PVN → era + packet IDs
│   │   ├── nbt.h/.cpp           # Minimal NBT writer
│   │   ├── registry.h/.cpp      # Configuration RegistryData + Finish
│   │   └── play.h/.cpp          # Play burst builders
│   ├── world/
│   │   ├── void_chunk.h/.cpp    # Empty chunk encoder per era
│   │   └── schematic.h/.cpp     # Sponge .schem v2 reader (planned)
│   └── security/
│       ├── limits.h/.cpp        # Rate limits, packet caps
│       └── velocity.h/.cpp      # MODERN HMAC-SHA256 verify
├── tests/
│   ├── test_*.cpp               # C++ unit tests
│   ├── integration/             # Python live-server tests
│   ├── fuzz/                    # Malformed packet corpus + harness
│   └── soak/                    # Long-run soak test
├── third_party/
│   ├── miniz.h/.c               # Vendored zlib (single-file)
├── tools/
│   └── gen_codec.py             # Codegen helper (optional)
├── Makefile
├── TODO.md
└── README.md
```

---

## Next Steps

- [Configuration](Configuration) — tune `limbo.properties`
- [Velocity Forwarding](Velocity-Forwarding) — secure proxy integration
- [Testing](Testing) — run the full test matrix
- [Architecture](Architecture) — understand the design