# Configuration

All settings live in `config/limbo.properties` (Java `.properties` format: `key=value`, `#` comments, no quotes).

---

## Complete Reference

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bind` | string | `0.0.0.0` | Listen address. `0.0.0.0` (IPv4 all), `::` (dual-stack IPv6 + IPv4), `127.0.0.1`, `::1`, or hostname. Resolved via `getaddrinfo`. |
| `port` | int | `25566` | TCP port. |
| `max_players` | int | `100` | Hard cap on concurrent connections. Excess connections are closed immediately after accept. |
| `version_name` | string | `Limbo-C++` | Shown in server list ping (`version.name`). |
| `motd` | string | `Limbo` | Message of the day (`description.text` in status response). Supports `\n` for multiline. |
| `protocol_max` | int | `774` | Maximum protocol version number to advertise in status. Clients with higher PVN will see "Outdated server". |
| `forwarding` | enum | `NONE` | `NONE` \| `MODERN`. `NONE` = offline UUID (`UUIDv3("OfflinePlayer:"+name)`). `MODERN` = Velocity HMAC forwarding. |
| `forwarding_secret` | hex string | *(empty)* | **Required if `forwarding=MODERN`**. 64 hex chars (32 bytes). Must match Velocity `player-information-forwarding.secret`. |
| `read_timeout_ms` | int | `30000` | Per-connection read deadline. Idle connections past this are closed. |
| `max_packet_bytes` | int | `8192` | Maximum decoded packet body length. Oversize packets → disconnect. |
| `max_packets_per_sec` | int | `200` | Rate limit: packets/second per connection. Excess → disconnect. |
| `max_bytes_per_sec` | int | `1048576` | Rate limit: bytes/second per connection (1 MiB/s). Excess → disconnect. |
| `spawn_x` | double | `8.5` | Spawn X coordinate. |
| `spawn_y` | double | `400.0` | Spawn Y coordinate (high void). |
| `spawn_z` | double | `8.5` | Spawn Z coordinate. |
| `gamemode` | int | `3` | Gamemode in `JoinGame`: `0=Survival`, `1=Creative`, `2=Adventure`, `3=Spectator`. |
| `view_distance` | int | `2` | Client view distance (chunks). Sent in `JoinGame`. |
| `dimension` | string | `minecraft:overworld` | Dimension registry key sent in `JoinGame` (1.20.2+). |
| `level_type` | string | `minecraft:the_void` | Level type (pre-1.18) / dimension type (1.18+). |
| `schematic_path` | string | *(empty)* | Path to `.schem` v2 file. If set and readable, pasted at spawn instead of void chunks. |
| `schematic_offset_x` | int | `0` | Paste offset X (blocks). |
| `schematic_offset_y` | int | `0` | Paste offset Y (blocks). |
| `schematic_offset_z` | int | `0` | Paste offset Z (blocks). |

---

## Example: Production Behind Velocity

```properties
# Network
bind=::
port=25566
max_players=500

# Identity
version_name=MyProxy Limbo
motd=Welcome to the lobby!\nMaintenance in 10 min
protocol_max=774

# Velocity MODERN forwarding
forwarding=MODERN
forwarding_secret=a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef

# Hardening
read_timeout_ms=20000
max_packet_bytes=4096
max_packets_per_sec=100
max_bytes_per_sec=524288

# World
spawn_x=0.5
spawn_y=256.0
spawn_z=0.5
gamemode=3
view_distance=2
dimension=minecraft:overworld

# Schematic (optional)
# schematic_path=/data/lobby.schem
# schematic_offset_x=-50
# schematic_offset_y=0
# schematic_offset_z=-50
```

---

## Example: Development / Direct Connect

```properties
bind=127.0.0.1
port=25565
max_players=10
version_name=Limbo-C++ dev
motd=Dev instance
protocol_max=774
forwarding=NONE
read_timeout_ms=60000
max_packet_bytes=8192
```

---

## IPv6 Notes

- `bind=::` creates a **single dual-stack socket** (`IPV6_V6ONLY=0`).
- IPv4 connections appear as `::ffff:a.b.c.d`; logged as dotted-quad via `peerIp` normalization.
- Verified with `tests/integration/ipv6_status.py` against `::1` and `127.0.0.1`.
- If your OS disables dual-stack (`net.ipv6.bindv6only=1`), use two configs on two ports or set `bind=0.0.0.0`.

---

## Rate Limit Tuning

| Scenario | `max_packets_per_sec` | `max_bytes_per_sec` |
|----------|----------------------|---------------------|
| Vanilla idle (keepalive only) | 10 | 1 KB/s |
| Normal play (movement, chat) | 50–100 | 50–100 KB/s |
| Aggressive client / bot | 200+ | 500 KB/s+ |
| **Recommended default** | **100** | **1 MiB/s** |

Adjust based on soak test (`tests/soak/soak.py`) observations.

---

## Reloading

**Not supported at runtime.** Edit `limbo.properties` and restart the process. Designed for proxy-managed rolling restarts.

---

## Validation Rules (Startup)

| Condition | Exit Code | Log |
|-----------|-----------|-----|
| `forwarding=MODERN` but `forwarding_secret` empty | 1 | `forwarding=MODERN requires forwarding_secret` |
| `forwarding_secret` not 64 hex chars | 1 | `forwarding_secret must be 64 hex characters (32 bytes)` |
| `port` < 1 or > 65535 | 1 | `invalid port` |
| `bind` resolves to no addresses | 1 | `failed to resolve bind address` |
| `schematic_path` set but file unreadable | 1 | `failed to open schematic` |

---

## Environment Variable Overrides (Not Yet Implemented)

Planned: `LIMBO_BIND`, `LIMBO_PORT`, `LIMBO_FORWARDING_SECRET`, etc. for container deployments. Tracked on the internal task board.