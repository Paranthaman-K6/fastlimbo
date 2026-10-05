# Protocol Support

> **Source:** packet IDs were cross-checked against the public protocol references listed in [Architecture](Architecture.md) and encoded as data in [`src/protocol/versions.h`](https://github.com/Paranthaman-K6/limbo/blob/main/src/protocol/versions.h).

---

## Supported Protocol Versions

| Minecraft Version | PVN | Era | Status |
|-------------------|-----|-----|--------|
| 1.8.x | 47 | `LEGACY_1_8` | ✅ Status + Login + Play |
| 1.12.2 | 340 | `V1_12` | ✅ Status + Login + Play |
| 1.13–1.15 | 393–578 | `V1_13_1_15` | ✅ Status + Login + Play |
| 1.16.x | 735–754 | `V1_16` | ✅ Status + Login + Play |
| 1.17–1.18 | 755–758 | `V1_17_1_18` | ✅ Status + Login + Play |
| 1.19.x | 759–762 | `V1_19` | ✅ Status + Login + Play |
| 1.20 / 1.20.1 | 763 | `V1_20_0_1` | ✅ Status + Login + Play |
| 1.20.2 | 764 | `V1_20_2` | ✅ + Configuration |
| 1.20.3 / 1.20.4 | 765 | `V1_20_3_4` | ✅ + GameEvent 13 |
| 1.20.5 / 1.20.6 | 766 | `V1_20_5_6` | ✅ |
| 1.21 / 1.21.1 | 767 | `V1_21_0_1` | ✅ Primary test target |
| 1.21.2 / 1.21.3 | 768–769 | `V1_21_2_3` | 🟡 Matrix pending |
| 1.21.4+ | 770+ | `V1_21_4_PLUS` | 🟡 Matrix pending |

**`protocol_max` in config** controls the highest PVN advertised in status response.

---

## State Machine by Era

### Pre-1.20.2 (PVN < 764)

```
Handshake (next=2) → Login Start → [Velocity Plugin] → Login Success → PLAY
                                                    ↓
                                            (optional Status if next=1)
```

### 1.20.2+ (PVN ≥ 764)

```
Handshake (next=2) → Login Start → [Velocity Plugin] → Login Success
                                                             ↓
                                                  Login Acknowledged (C→S)
                                                             ↓
                                                    Configuration State
                                                  Plugin Message (brand)
                                                  Registry Data (dim/biome/chat)
                                                  Finish Configuration
                                                             ↓
                                                  Finish Config Ack (C→S)
                                                             ↓
                                                            PLAY
```

---

## Packet ID Tables (Clientbound Play)

> **Source:** `PlayIds playIds(int pvn)` in `versions.h`. Verified against NanoLimbo `State.java`, PicoLimbo `packets.json`, Elytrium `LimboProtocol`.

| Era | JoinGame | KeepAlive | Position | Chunk | Disconnect | Abilities | GameEvent | CenterChunk |
|-----|----------|-----------|----------|-------|------------|-----------|-----------|-------------|
| `LEGACY_1_8` | 0x01 | 0x00 | 0x08 | 0x21 | 0x40 | 0x39 | — | — |
| `V1_12` | 0x23 | 0x1F | 0x2F | 0x20 | 0x1A | 0x2C | — | — |
| `V1_13_1_15` | 0x25 | 0x20 | 0x35 | 0x21 | 0x1A | 0x30 | — | — |
| `V1_16` | 0x24 | 0x1F | 0x34 | 0x20 | 0x19 | 0x30 | — | 0x40 |
| `V1_17_1_18` | 0x26 | 0x1F | 0x38 | 0x22 | 0x1A | 0x32 | — | 0x40 |
| `V1_19` | 0x28 | 0x23 | 0x3C | 0x24 | 0x1A | 0x34 | — | 0x48 |
| `V1_20_0_1` | 0x28 | 0x23 | 0x3C | 0x24 | 0x1A | 0x34 | — | 0x4E |
| `V1_20_2` | 0x29 | 0x24 | 0x3E | 0x25 | 0x1B | 0x36 | 0x20 | 0x4E |
| `V1_20_3_4` | 0x29 | 0x24 | 0x3E | 0x25 | 0x1B | 0x36 | 0x20 | 0x4E |
| `V1_20_5_6` | 0x2B | 0x26 | 0x40 | 0x27 | 0x1D | 0x38 | 0x22 | 0x52 |
| `V1_21_0_1` | 0x2B | 0x26 | 0x40 | 0x27 | 0x1D | 0x38 | 0x22 | 0x54 |
| `V1_21_2_3` | 0x2C | 0x27 | 0x42 | 0x28 | 0x1C | 0x3A | 0x23 | 0x56 |
| `V1_21_4_PLUS` | 0x2B | 0x26 | 0x41 | 0x27 | 0x1C | 0x39 | 0x22 | 0x54 |

### Serverbound KeepAlive (Client Echo)

| Era | Packet ID |
|-----|-----------|
| `LEGACY_1_8` | 0x00 |
| `V1_12` | 0x0B |
| `V1_16` | 0x10 |
| `V1_19` / `V1_20_0_1` | 0x12 |
| `V1_20_2` / `V1_20_3_4` | 0x14 |
| `V1_20_5_6` / `V1_21_0_1` | 0x18 |
| `V1_21_2_3` | 0x1A |
| `V1_21_4_PLUS` | 0x1B |

---

## Login Plugin Request (Velocity MODERN)

### Velocity → Backend (Clientbound Login)

| Field | Type | Value |
|-------|------|-------|
| `messageId` | VarInt | Unique per request |
| `channel` | String | `"velocity:player_info"` |
| `data` | Byte Array | Empty |

### Backend → Velocity (Serverbound Login) — `Login Plugin Response` (0x02)

| Field | Type | Value |
|-------|------|-------|
| `messageId` | VarInt | Echo from request |
| `successful` | Boolean | `true` if HMAC valid |
| `data` | Byte Array | Empty (success) / error message (failure) |

### Payload (Inside HMAC)

```
HMAC-SHA256(32 bytes) || payload
```

**Payload layout:**

| Field | Type | Notes |
|-------|------|-------|
| `version` | VarInt | 1–4 (backend must accept all) |
| `address` | String | Player IP (IPv4 or IPv6, no `%zone`) |
| `uuid` | UUID (16 bytes) | Player UUID |
| `username` | String | Player name |
| `properties` | Array | `[{name, value, signed?, signature?}]` |

---

## Configuration State (PVN ≥ 764)

### Required Registry Data

| Registry | Key | Minimal Entry |
|----------|-----|---------------|
| `dimension_type` | `minecraft:overworld` | `fixed_time=-1`, `has_skylight=true`, `has_ceiling=false`, `ambient_light=0.0`, `infiniburn='#minecraft:infiniburn_overworld'`, `respawn_anchor_works=false`, `coordinate_scale=1.0`, `ultrawarm=false`, `natural=true`, `piglin_safe=false`, `bed_works=true`, `has_raids=true`, `logical_height=384`, `min_y=-64` |
| `biome` | `minecraft:plains` | `temperature=0.8`, `downfall=0.4`, `precipitation=rain`, `category=plains`, `depth=0.125`, `scale=0.05` |
| `chat_type` | `minecraft:chat` | `chat=translation_key`, `narration=translation_key` |

**Encoding:** NBT unnamed compound → `RegistryData` packet per registry.

---

## Void Chunk Format by Era

| Era | Chunk Format | Key Requirements |
|-----|--------------|------------------|
| `<1.14` | `Map Chunk` (bulk) | Zero sections, full bright |
| `1.14–1.17` | `Chunk Data` (0x21/0x22) | Bitmask=0, heightmaps `MOTION_BLOCKING`=0, no entities |
| `1.18+` | `Chunk Data` + 3D biomes | Biome palette: single `minecraft:plains`, heightmaps, `Update Light` full-bright |
| `1.20.2+` | As 1.18+ | Registry Data must already define `dimension_type`/`biome` |

**All eras:** Exactly one chunk at chunk coordinates (0, 0). Client never leaves it.

---

## GameEvent 13 (PVN ≥ 765)

| Field | Value |
|-------|-------|
| `event` | 13 (`minecraft:start_waiting_for_level_chunks`) |
| `value` | 0.0 (float) |

**Required before/with first chunk** or client hangs on "Loading terrain...". Backported by all maintained limbos.

---

## KeepAlive Timing

| Implementation | Interval | Timeout |
|----------------|----------|---------|
| NanoLimbo | 10 s | 30 s (3 missed) |
| Vanilla | 15 s | 30 s |
| limbo-c++ | 10 s | `read_timeout_ms` (default 30 s) |

Server sends `KeepAlive` (clientbound) with random `ID` (int64). Client must echo with same `ID` (serverbound). Missed → disconnect.

---

## Status Response JSON

```json
{
  "version": {
    "name": "Limbo-C++",
    "protocol": 767
  },
  "players": {
    "max": 100,
    "online": 0,
    "sample": []
  },
  "description": {
    "text": "Limbo-C++ void"
  },
  "favicon": null
}
```

---

## Version Detection Flow

```cpp
// In connection.cpp handshake handler
int pvn = handshake.protocol_version;
Era era = eraForPvn(pvn);

if (pvn > config.protocol_max) {
    sendDisconnect("Outdated server");  // or "Outdated client" depending on side
    return;
}

if (handshake.next_state == 1) {
    handleStatus(pvn);
} else if (handshake.next_state == 2) {
    handleLogin(pvn, era);
}
```

---

## Testing Matrix

Run via `tests/integration/matrix.py`:

```sh
# Test specific version
python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 --version 767

# Test all supported versions (requires Velocity forwarding for 1.13+)
python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 --all

# Test with Velocity MODERN
python3 tests/integration/matrix.py --host 127.0.0.1 --port 25566 --forwarding modern --secret <hex>
```

### Matrix Coverage (Target)

| PVN | Direct | Velocity MODERN | Void | Schematic |
|-----|--------|-----------------|------|-----------|
| 47 | ✅ | N/A | ✅ | — |
| 340 | ✅ | N/A | ✅ | — |
| 754 | ✅ | ✅ | ✅ | 🟡 |
| 762 | ✅ | ✅ | ✅ | 🟡 |
| 763 | ✅ | ✅ | ✅ | 🟡 |
| 764 | ✅ | ✅ | ✅ | 🟡 |
| 766 | ✅ | ✅ | ✅ | 🟡 |
| 767 | ✅ | ✅ | ✅ | 🟡 |

---

## References

- [wiki.vg Protocol](https://wiki.vg/Protocol)
- [minecraft.wiki Java Edition Protocol](https://minecraft.wiki/w/Java_Edition_protocol)
- [PrismarineJS minecraft-data](https://github.com/PrismarineJS/minecraft-data)
- [NanoLimbo](https://github.com/Nan1t/NanoLimbo)
- [PicoLimbo](https://github.com/Quozul/PicoLimbo)
- [LOOHP Limbo](https://github.com/LOOHP/Limbo)
- [Elytrium LimboAPI](https://github.com/Elytrium/LimboAPI)
- [Velocity Forwarding Docs](https://docs.papermc.io/velocity/player-information-forwarding)