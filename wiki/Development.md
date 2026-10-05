# Development

> **Workflow:** Multi-agent, lead-merges-connection.cpp, agents-own-new-files-only

---

## Code Layout

```
src/
├── main.cpp                 # Entry point, config load, startup validation
├── config.h/.cpp            # Properties parser → Config struct
├── server/
│   ├── tcp_server.h/.cpp    # Listener: socket, bind, listen, accept loop
│   └── connection.h/.cpp    # **LEAD-ONLY** Per-fd state machine, rate limits, protocol dispatch
├── protocol/
│   ├── buffer.h/.cpp        # Read/write cursor, VarInt, String, UUID, framing
│   ├── varint.h             # ZigZag, VarInt encode/decode (header-only)
│   ├── packets.h/.cpp       # Packet builders (Login/Config/Play/Status)
│   ├── versions.h           # **DATA-DRIVEN** Era + packet ID tables
│   ├── nbt.h/.cpp           # Minimal NBT writer (unnamed compound + primitives)
│   ├── registry.h/.cpp      # Configuration RegistryData + Finish
│   └── play.h/.cpp          # Play burst builders (JoinGame, chunk, keepalive)
├── world/
│   ├── void_chunk.h/.cpp    # Era-correct empty chunk encoder
│   └── schematic.h/.cpp     # Sponge .schem v2 reader (planned)
└── security/
    ├── limits.h/.cpp        # Rate limits (pps, Bps), max_packet_bytes
    └── velocity.h/.cpp      # MODERN HMAC-SHA256 verify (constant-time)
```

---

## Ownership Rules

| File | Owner | Rule |
|------|-------|------|
| `src/server/connection.cpp` | **Lead only** | Core state machine; agents MUST NOT edit |
| `src/server/tcp_server.cpp` | Lead only | Listener loop |
| `src/protocol/versions.h` | Shared | Data-driven tables; agents add entries for their eras |
| `src/protocol/packets.h/.cpp` | Shared | Packet builders; agents add for their packets |
| `src/world/void_chunk.*` | Play agent | Void chunk encoding |
| `src/world/schematic.*` | Schematic agent | `.schem` reader |
| `src/protocol/nbt.*` | Registry agent | NBT writer |
| `src/protocol/registry.*` | Registry agent | Configuration RegistryData |
| `src/protocol/play.*` | Play agent | Play burst builders |
| `src/security/velocity.*` | Security agent | MODERN HMAC |
| `src/security/limits.*` | Hardening agent | Rate limits |
| `tests/test_*.cpp` | Agent owning module | Unit tests for owned code |
| `tests/integration/*.py` | Shared | Integration test helpers |
| `tests/fuzz/*.py` | Hardening agent | Fuzz harness |

**Enforced by:** Makefile `SRC` filter excludes agent-owned `.cpp` from main `limbo` target until lead merges.

---

## Build System

### Makefile Targets

```make
# Main server (excludes agent-owned files until merged)
limbo: $(MAIN_OBJS)
	$(CXX) -o build/limbo $^ $(LDFLAGS)

# Unit tests (each agent builds their own)
test_varint: tests/test_varint.cpp src/protocol/varint.h
test_buffer: tests/test_buffer.cpp src/protocol/buffer.cpp ...
test_velocity: tests/test_velocity.cpp src/security/velocity.cpp ...
# ...

test: test_varint test_buffer test_registry test_void_chunk test_velocity test_limits test_nbt
	./build/test_varint && ./build/test_buffer && ...
```

### Adding a New Module

1. Create `src/<module>/feature.h/.cpp`
2. Create `tests/test_feature.cpp`
3. Add to Makefile: `test_feature` target + add to `test:` deps
4. Agent builds/tests independently: `make test_feature && ./build/test_feature`
5. Lead merges: add `.cpp` to `MAIN_OBJS` in Makefile

---

## Code Style

### C++20 Features Used

| Feature | Example |
|---------|---------|
| `std::format` | Logging: `std::format("[limbo] peer={} handshake pvn={}", peerIp, pvn)` |
| `std::span` | Buffer views: `std::span<const uint8_t> payload(buf.data() + pos, len)` |
| `std::array` | Fixed-size: `std::array<uint8_t, 16> uuid` |
| `std::optional` | Nullable: `std::optional<Config> loadConfig(path)` |
| `std::byte` | Raw bytes: `std::vector<std::byte>` for packet bodies |
| Structured bindings | `auto [id, name] = parsePacket(buf);` |
| `if constexpr` | Compile-time branches: `if constexpr (sizeof(T) == 1)` |

### Formatting

```sh
# If clang-format available
make fmt
# Uses .clang-format (to be added) or LLVM style
```

### Naming

| Kind | Convention | Example |
|------|------------|---------|
| Namespace | `lowercase` | `limbo::proto`, `limbo::server` |
| Class/Struct | `PascalCase` | `PlayIds`, `Buffer`, `Config` |
| Function | `camelCase` | `readVarInt`, `sendJoinGame` |
| Variable | `snake_case` | `max_players`, `peer_ip` |
| Constant | `kPascalCase` | `kMaxPacketBytes` |
| Enum | `PascalCase` | `Era`, `ForwardingMode` |
| Enum value | `SCREAMING_SNAKE` | `V1_21_0_1`, `MODERN` |

### Error Handling

- **No exceptions** — use return codes / `std::optional` / `std::expected` (C++23, polyfill for now)
- **Log + disconnect** for protocol errors — never crash
- **Assert** for programmer errors (impossible states) — `assert()` in debug, no-op in release

```cpp
// Good: protocol error → disconnect
if (!buf.readVarInt(packetId)) {
    log("peer={} varint overflow", peerIp);
    return disconnect(fd, "Malformed packet");
}

// Good: programmer error → assert
assert(pvn >= 0 && "protocol version must be non-negative");
```

---

## Logging

```cpp
// Simple fprintf to stderr with [limbo] prefix
// Format: [limbo] peer=1.2.3.4:12345 message
// Levels: none (production), debug (dev)
```

**No logging library** — stderr is captured by proxy/systemd.

---

## Adding a New Protocol Version

1. **Research:** Check `minecraft-data` `protocol.json` for new PVN
2. **Update `versions.h`:**
   - Extend `eraForPvn()` range or add new `Era`
   - Add `PlayIds` entry in `playIds()`
   - Add `playKeepAliveServerbound()` entry
   - Add `versionName()` entry
3. **Update void chunk** if format differs (`void_chunk.cpp`)
4. **Update Configuration** if new registries (`registry.cpp`)
5. **Test:** `matrix.py --version <new_pvn>`
6. **Document:** Update [Protocol Support](Protocol-Support.md) table

---

## Multi-Agent Workflow

### Lead Responsibilities

- Owns `connection.cpp`, `tcp_server.cpp`, `Makefile` `MAIN_OBJS`
- Merges agent PRs after verification
- Runs full test matrix before merge
- Updates `TODO.md` checkboxes

### Agent Responsibilities

- Own **new files only** in their domain
- **Never edit** `connection.cpp`, `tcp_server.cpp`, `main.cpp`
- Deliver: `.h/.cpp` + `test_*.cpp` + docs update
- Build independently: `make test_<module>`
- Background session — no trunk edits

### Merge Checklist (Lead)

- [ ] Agent's `make test_<module>` passes
- [ ] Agent's `test_<module>` binary runs clean
- [ ] No compile warnings in agent's files
- [ ] Docs updated (wiki + `docs/`)
- [ ] Add `.cpp` to `MAIN_OBJS` in Makefile
- [ ] Run `make test` (full unit suite)
- [ ] Run `matrix.py --all` (direct)
- [ ] Run `matrix.py --all --forwarding modern` (Velocity)
- [ ] Update `TODO.md` checkbox

---

## Debugging

### GDB

```sh
gdb ./build/limbo
(gdb) run --config config/limbo.properties
# On crash: bt, info locals, print variables
```

### Valgrind / ASAN

```sh
# Valgrind (slow but thorough)
valgrind --leak-check=full --show-leak-kinds=all ./build/limbo --config config/limbo.properties

# ASAN (fast, catches buffer overflows)
CXXFLAGS="-fsanitize=address -fno-omit-frame-pointer -g" make clean all
./build/limbo --config config/limbo.properties
```

### Packet Capture

```sh
# tcpdump on loopback
sudo tcpdump -i lo -nn -s0 -w limbo.pcap port 25566
# Open in Wireshark: "minecraft" protocol dissector (if available) or raw TCP
```

### Print Raw Packets

```cpp
// In connection.cpp read loop (temporarily)
std::string hex;
for (uint8_t b : buf) hex += std::format("{:02x} ", b);
fprintf(stderr, "[limbo] peer=%s RAW(%zu): %s\n", peerIp.c_str(), buf.size(), hex.c_str());
```

---

## Profiling

```sh
# perf (Linux)
perf record -g ./build/limbo --config config/limbo.properties
# ... run load test ...
perf report

# heaptrack (memory)
heaptrack ./build/limbo --config config/limbo.properties
# ... run soak ...
heaptrack_gui heaptrack.limbo.*.gz
```

---

## Dependencies

| Dep | Version | Purpose | Vendored? |
|-----|---------|---------|-----------|
| `g++` | ≥12 | C++20 compiler | No |
| `make` | Any | Build | No |
| OpenSSL `libcrypto` | 3.x | HMAC-SHA256 | No (system) |
| `miniz` | Latest | gzip for `.schem` | **Yes** (`third_party/`) |

**No:** Boost, CMake, zlib-dev, nlohmann/json, spdlog, fmt (use `std::format`)

---

## Release Process (Planned)

1. Update `version_name` in `config/limbo.properties` (or compile-time constant)
2. Run full acceptance: `make test && matrix.py --all --forwarding modern && soak.py`
3. Tag: `git tag v0.x.x`
4. Build release binary: `make clean && CXXFLAGS="-O3 -DNDEBUG" make`
5. Package: `build/limbo` + `config/limbo.properties.example` + `README.md`
6. GitHub Release with binary + checksums

---

## Contributing

1. Read [Architecture](Architecture.md) and [Protocol Support](Protocol-Support.md)
2. Pick a task item from the internal task board or propose new
3. Follow ownership rules — new files only, no `connection.cpp` edits
4. Write unit test + integration test hook
5. Update relevant wiki page
6. Submit via lead merge process

---

## Related

- [Architecture](Architecture.md) — design decisions
- [Protocol Support](Protocol-Support.md) — version tables
- [Testing](Testing.md) — test commands
- [Hardening & Security](Hardening-Security.md) — limits, fuzzing