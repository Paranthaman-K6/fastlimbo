# Troubleshooting

---

## Startup Failures

| Error | Cause | Fix |
|-------|-------|-----|
| `forwarding=MODERN requires forwarding_secret` | `forwarding=MODERN` but secret empty | Set `forwarding_secret` (64 hex chars) |
| `forwarding_secret must be 64 hex characters` | Secret wrong length | `openssl rand -hex 32` |
| `invalid port` | Port < 1 or > 65535 | Set valid `port` in config |
| `failed to resolve bind address` | `bind` hostname unresolvable | Use IP (`0.0.0.0`, `::`, `127.0.0.1`) or fix DNS |
| `failed to open schematic` | `schematic_path` set but file missing/unreadable | Check path, permissions, or clear `schematic_path` |
| `bind: Address already in use` | Port occupied | Change `port` or stop other process |
| `bind: Permission denied` | Port < 1024 without root | Use port >= 1024 (e.g., 25566) or `setcap` |

---

## Connection Issues

### Client: "Connection refused"

| Check | Command |
|-------|---------|
| Server running? | `ps aux \| grep limbo` |
| Listening on port? | `ss -ltnp \| grep 25566` |
| Correct bind? | `bind=0.0.0.0` for all interfaces |
| Firewall? | `sudo ufw status` / `iptables -L` |

### Client: "Connection timed out"

| Check | Command |
|-------|---------|
| Server reachable? | `telnet <host> 25566` / `nc -zv <host> 25566` |
| Proxy forwarding? | Check Velocity `servers.limbo` target |
| IPv6 mismatch? | Try `bind=::` for dual-stack, or explicit `bind=0.0.0.0` |

### Client: "Outdated server" / "Outdated client"

| Config | Meaning |
|--------|---------|
| `protocol_max=774` | Server advertises max PVN 774 (1.21.11+) |
| Client PVN > 774 | Client newer than server -> "Outdated server" |
| Server PVN < client | Not possible -- server advertises max |

**Fix:** Increase `protocol_max` to match latest client, or use compatible client version.

---

## Login Issues

### Stuck at "Logging in..."

| Log | Cause | Fix |
|-----|-------|-----|
| No log after "login start" | Handshake PVN not recognized | Check `eraForPvn()` in `versions.h` |
| "velocity:player_info" sent but no response | Velocity not configured for MODERN | Check Velocity `player-information-forwarding.mode = "MODERN"` |
| "plugin response success=false" | HMAC mismatch | Verify `forwarding_secret` matches Velocity exactly |
| "Invalid forwarding signature" | HMAC verify failed | Same secret? Constant-time compare? Check `velocity.cpp` |
| "Unsupported forwarding version" | Velocity sends version > 4 | Update `velocity.cpp` to accept newer versions |

### "Failed to verify username" / "Invalid session"

- **Not applicable** -- limbo-c++ is offline-mode / Velocity MODERN only
- No Mojang auth, no encryption
- If seeing this, client is talking to wrong server (not limbo)

---

## Play Issues

### "Loading terrain..." forever (PVN >= 765)

| Cause | Fix |
|-------|-----|
| Missing `GameEvent 13` | Ensure `needsGameEvent13(pvn)` branch sends it before/with chunk |
| Chunk format wrong for era | Check `void_chunk.cpp` for PVN >= 765 biome palette + heightmaps |

### "Invalid chunk data" / Chunk reject

| Era | Requirement |
|-----|-------------|
| 1.18+ (PVN >= 755) | Biome palette with `minecraft:plains` |
| 1.20.2+ (PVN >= 764) | Configuration `RegistryData` for `dimension_type`, `biome` sent before Play |

### Player falls into void / takes damage

| Check | Fix |
|-------|-----|
| `gamemode=3` (Spectator) | Set in config |
| `spawn_y=400` | High enough above build limit |
| `PlayerAbilities` flying+invulnerable | Verify `play.cpp` sends correct flags |

### KeepAlive timeout / "Timed out"

| Config | Check |
|--------|-------|
| `read_timeout_ms=30000` | Increase for high latency (60000) |
| Network latency | `ping <host>` -- if > 10s, increase timeout |
| Client not echoing | Check client version supports KeepAlive (all do) |

---

## Velocity Forwarding Issues

### "Invalid forwarding signature" (HMAC fail)

```sh
# Debug: verify secret matches
# Velocity velocity.toml:
#   secret = "a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef"
# limbo limbo.properties:
#   forwarding_secret=a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef

# Test vector (Python)
python3 -c "
import hmac, hashlib
secret = bytes.fromhex('a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef')
payload = b'\x03\x07127.0.0.1' + b'\x00'*16 + b'\x05Notch\x00'
print(hmac.new(secret, payload, hashlib.sha256).digest().hex())
"
# Compare with limbo log (add debug logging temporarily)
```

### Velocity sends legacy forwarding instead of MODERN

- Velocity `velocity.toml` must have `mode = "MODERN"` (not `LEGACY` or `BUNGEEGUARD`)
- Requires Velocity 3.0+

### IPv6 `%zone` in forwarded address

- limbo-c++ strips `%zone` automatically (`velocity.cpp:stripZoneId`)
- If seeing in logs, check `velocity.cpp` is current

---

## Performance Issues

### High Memory (RSS)

| Symptom | Likely Cause | Check |
|---------|--------------|-------|
| RSS grows with connections | FD leak / thread stack | `lsof -p <pid>`, `pmap -x <pid>` |
| RSS > 256MB at 1k idle | Thread stack (default 8MB) | `ulimit -s 256` or `pthread_attr_setstacksize` |
| RSS spikes on join | Large allocation in burst | Check `Buffer` reserve, `void_chunk` encoding |

### High CPU

| Symptom | Likely Cause |
|---------|--------------|
| CPU high at idle | Busy loop in connection (check `read` timeout) |
| CPU spikes on packet | VarInt decode loop (check `buffer.cpp`) |
| CPU in HMAC | OpenSSL not using hardware acceleration |

### Join Latency High

| Target | Check |
|--------|-------|
| P99 < 50ms | `soak.py` measures this |
| Slow handshake | DNS resolve in `bind=` hostname? Use IP |
| Slow Login | HMAC verify (should be < 1ms) |
| Slow Play burst | Chunk encoding (should be < 5ms) |

---

## Build Issues

### `g++: error: unrecognized command-line option '-std=c++20'`

```sh
g++ --version  # Need >= 10 for c++20, >= 12 recommended
# Install: apt install g++-12 / dnf install gcc-c++ / brew install gcc
```

### `fatal error: openssl/hmac.h: No such file or directory`

```sh
# Ubuntu/Debian
sudo apt install libssl-dev
# Fedora/RHEL
sudo dnf install openssl-devel
# Arch
sudo pacman -S openssl
```

### `undefined reference to 'HMAC'` / `CRYPTO_memcmp`

```sh
# Link libcrypto
LDFLAGS="-lcrypto -lpthread"
# Verify
ldd build/limbo | grep crypto
```

### `miniz.h: No such file`

```sh
# Should be in third_party/
ls third_party/miniz.h
# Makefile has -Ithird_party
```

### Multiple definition errors (agent merge)

- Agent edited `connection.cpp` -> **revert**, use own files only
- Lead merges `connection.cpp` after agent delivers new files

---

## Test Failures

### `make test` fails

| Test | Common Fix |
|------|------------|
| `test_varint` | ZigZag edge cases (INT_MIN, INT_MAX) |
| `test_buffer` | String length > 32767, VarInt overflow |
| `test_velocity` | HMAC vector mismatch -- check secret/endianness |
| `test_limits` | Sliding window bucket rollover |
| `test_nbt` | Nested compound depth, string length |

### `matrix.py` fails

| Failure | Debug |
|---------|-------|
| "Handshake timeout" | Server not listening / wrong port |
| "LoginSuccess missing" | Packet ID wrong for PVN -- check `versions.h` |
| "Configuration stall" | Missing `RegistryData` / `Finish Configuration` |
| "GameEvent 13 missing" | PVN >= 765 but not sent |
| "Chunk rejected" | Biome palette / heightmaps wrong for era |
| "KeepAlive echo fail" | Serverbound ID mismatch -- check `playKeepAliveServerbound()` |

### `fuzz_packet.py` crashes

- Run with ASAN: `CXXFLAGS="-fsanitize=address,undefined" make clean all`
- Crash = bug in parsing (buffer overflow, integer overflow, etc.)
- Fix in relevant module (`buffer.cpp`, `nbt.cpp`, `velocity.cpp`)

---

## Log Reference

### Normal Startup

```
[limbo] bind=0.0.0.0 port=25566 max_players=100 version=Limbo-C++ proto_max=774 forwarding=NONE
[limbo] listening on 0.0.0.0:25566 (fd=3)
```

### Normal Join (Direct, PVN 767)

```
[limbo] peer=127.0.0.1:12345 handshake pvn=767 state=1
[limbo] peer=127.0.0.1:12345 status request
[limbo] peer=127.0.0.1:12345 ping echo=1234567890
[limbo] peer=127.0.0.1:12346 handshake pvn=767 state=2
[limbo] peer=127.0.0.1:12346 login start username=Notch
[limbo] peer=127.0.0.1:12346 login success uuid=offline-... username=Notch
[limbo] peer=127.0.0.1:12346 play burst done
[limbo] peer=127.0.0.1:12346 keepalive echo ok
```

### Normal Join (Velocity MODERN, PVN 767)

```
[limbo] peer=127.0.0.1:12345 handshake pvn=767 state=2
[limbo] peer=127.0.0.1:12345 login start username=Notch
[limbo] peer=127.0.0.1:12345 login plugin request msgId=42
[limbo] peer=127.0.0.1:12345 plugin response success=true len=98
[limbo] peer=127.0.0.1:12345 forwarded: uuid=... username=Notch addr=127.0.0.1 version=3
[limbo] peer=127.0.0.1:12345 login success uuid=... username=Notch
[limbo] peer=127.0.0.1:12345 play burst done
```

### Rate Limit Hit

```
[limbo] peer=1.2.3.4:12345 rate limit exceeded packets=150 limit=100
[limbo] peer=1.2.3.4:12345 disconnecting: rate limit
```

### Read Timeout

```
[limbo] peer=1.2.3.4:12345 read timeout (30000ms)
[limbo] peer=1.2.3.4:12345 disconnecting: timeout
```

### Malformed Packet

```
[limbo] peer=1.2.3.4:12345 varint overflow (5+ bytes)
[limbo] peer=1.2.3.4:12345 disconnecting: malformed packet
```

---

## Getting Help

1. **Check logs** -- stderr has `[limbo]` prefix
2. **Run integration test** -- `matrix.py` isolates version-specific issues
3. **Check wiki** -- [Protocol Support](Protocol-Support), [Velocity Forwarding](Velocity-Forwarding), [Void World](Void-World)
4. **Enable debug** -- add temporary `fprintf` in `connection.cpp` (lead only)
5. **Capture packets** -- `tcpdump -i lo -w limbo.pcap port 25566`
6. **Open issue** -- include: config, logs, client version, PVN, test output

---

## Related

- [Configuration](Configuration) -- all config keys
- [Testing](Testing) -- test commands
- [Velocity Forwarding](Velocity-Forwarding) -- MODERN debugging
- [Hardening & Security](Hardening-Security) -- rate limits, timeouts