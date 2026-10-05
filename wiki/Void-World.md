# Void World

The "void world" is the minimal Play environment: a single empty chunk at (0, 0) in the Overworld, player spawned at Y=400 in Spectator mode with flight/invulnerability, periodic KeepAlive.

---

## Spawn Configuration

| Setting | Config Key | Default | Description |
|---------|------------|---------|-------------|
| X | `spawn_x` | `8.5` | Spawn X (block center) |
| Y | `spawn_y` | `400.0` | Spawn Y (high void, no fall damage) |
| Z | `spawn_z` | `8.5` | Spawn Z |
| Gamemode | `gamemode` | `3` | `3 = Spectator` |
| View Distance | `view_distance` | `2` | Chunks sent to client |
| Dimension | `dimension` | `minecraft:overworld` | Registry key (1.20.2+) |

**Why Y=400?** Well above build limit (384 in 1.18+), in void. Spectator mode prevents fall damage and movement.

---

## Play Burst Sequence

| Order | Packet | Purpose |
|-------|--------|---------|
| 1 | `Join Game` | Dimension, gamemode, spawn pos, view distance, reduced debug info |
| 2 | `Plugin Message (brand)` | `minecraft:brand` = "Limbo-C++" |
| 3 | `Player Abilities` | Flying, invulnerable, creative (spectator perms) |
| 4 | `Player Position and Look` | Teleport to spawn X/Y/Z, yaw=0, pitch=0 |
| 5 | `Game Event 13` | **PVN ≥ 765 only** — "Start waiting for level chunks" |
| 6 | `Chunk Data` | Single void chunk at chunk (0, 0) |
| 7 | `Keep Alive` | Loop every 10s; kick on timeout |

**Clientbound movement packets (teleport confirm, move vehicle, etc.) are answered. Player movement packets are silently dropped.**

---

## Void Chunk Encoding by Era

> **Source:** [`src/world/void_chunk.cpp`](../src/world/void_chunk.cpp), [docs/research/limbo-references.md §3](../docs/research/limbo-references.md#3-chunk--void--spawn-recipe)

### Pre-1.14 (PVN < 480ish) — Legacy `Map Chunk` / Bulk

- Not currently implemented (no test demand). Would use bulk chunk format with zero sections.

### 1.14–1.17 (PVN 480–754) — `Chunk Data` (0x21/0x22)

```cpp
// Simplified structure
ChunkData {
    int chunkX, chunkZ;        // 0, 0
    bool fullChunk = true;     // always
    VarInt primaryBitMask = 0; // no sections
    VarInt heightmaps = {      // compound NBT
        "MOTION_BLOCKING": LongArray(256 zeros)
    };
    VarInt biomeData = 0;      // not used pre-1.18
    int entityCount = 0;
    // no block entities
}
```

### 1.18+ (PVN ≥ 755) — `Chunk Data` + 3D Biomes + Light

```cpp
ChunkData {
    int chunkX, chunkZ;        // 0, 0
    VarInt primaryBitMask = 0; // no block sections
    VarInt heightmaps = {      // required
        "MOTION_BLOCKING": LongArray(256 zeros),
        "WORLD_SURFACE": LongArray(256 zeros),
        "OCEAN_FLOOR": LongArray(256 zeros)
    };
    // Biome palette: single entry "minecraft:plains"
    VarInt biomePalette = 1;
    VarInt biomeData = 4096 * 0; // all 0 (plains)
    // Light: full bright (all 0xF)
    VarInt skyLightMask = 0;   // no sections = no light arrays needed
    VarInt blockLightMask = 0;
    int entityCount = 0;
}
```

**Key difference 1.18+:** Biome palette is mandatory. Client rejects chunk if biome registry not defined (hence Configuration `RegistryData` for PVN ≥ 764).

### 1.20.2+ (PVN ≥ 764) — Registry Data Prerequisite

Before entering Play, Configuration state must send `Registry Data` for:
- `minecraft:dimension_type` (overworld)
- `minecraft:biome` (plains)
- `minecraft:chat_type` (chat)

If missing, client rejects the void chunk in Play.

---

## GameEvent 13 (PVN ≥ 765)

```cpp
GameEvent {
    float event = 13.0f;  // minecraft:start_waiting_for_level_chunks
    float value = 0.0f;
}
```

**Sent before or with first chunk.** Without it, 1.20.3+ clients hang on "Loading terrain...".

---

## KeepAlive Loop

| Parameter | Value |
|-----------|-------|
| Interval | 10 seconds |
| Timeout | `read_timeout_ms` (default 30s) |
| Payload | Random int64 (generated per ping) |
| Client echo | Must match ID exactly |

**Implementation:** Single `std::thread` per connection sleeps 10s, sends `KeepAlive`, updates `lastKeepAliveSent`. Read loop checks `now - lastKeepAliveRecv > timeout`.

---

## Player Abilities (Spectator)

```cpp
PlayerAbilities {
    bool invulnerable = true;
    bool flying = true;
    bool allowFlying = true;
    bool creativeMode = true;  // spectator uses creative perms
    float flySpeed = 0.05f;
    float walkSpeed = 0.1f;
}
```

---

## Movement Handling

| Client Packet | Server Response |
|---------------|-----------------|
| `Player Position` / `Position and Look` / `Rotation` / `On Ground` | **Dropped** (ignored) |
| `Teleport Confirm` | Acknowledged (echo teleport ID) |
| `Vehicle Move` | Dropped |
| `Steer Vehicle` | Dropped |
| `Player Action` (dig, drop, etc.) | Dropped |
| `Use Item` / `Interact` | Dropped |

**Rationale:** Limbo is a holding pen. No world interaction needed.

---

## Schematic Override (Planned)

When `schematic_path` is set and valid:

1. Load `.schem` v2 at startup (Sponge format, zlib + NBT)
2. On Play join, paste at `spawn_x + offset_x`, `spawn_y + offset_y`, `spawn_z + offset_z`
3. Send pasted chunks instead of void chunk(s)
4. Fallback to void if schematic missing/corrupt/oversize

**Size caps (planned):**
- Max 16×16×16 chunks (4096 chunks)
- Max 64 MiB decoded NBT
- Parse timeout 5s

See [Schematic Support](Schematic-Support).

---

## Debugging Void World

### Check Chunk Packet (Wireshark / proxy log)

```
Chunk Data:
  chunkX=0, chunkZ=0
  bitMask=0
  heightmaps: MOTION_BLOCKING=[0,0,...] (256 zeros)
  biomes: palette=["minecraft:plains"], data=[0,0,...] (4096 zeros)
  blockEntities: 0
  entities: 0
```

### Verify GameEvent 13 (PVN ≥ 765)

```
Game Event:
  event=13 (start_waiting_for_level_chunks)
  value=0.0
```

### Verify KeepAlive

```
S→C KeepAlive: id=1234567890123456789
C→S KeepAlive: id=1234567890123456789  (echo)
```

---

## Common Issues

| Issue | Cause | Fix |
|-------|-------|-----|
| Client "Loading terrain..." forever | Missing GameEvent 13 (PVN ≥ 765) | Ensure `needsGameEvent13(pvn)` branch sends it |
| Client "Invalid chunk data" | Biome palette missing (1.18+) | Add biome palette with `minecraft:plains` |
| Client disconnects after JoinGame | Dimension/biome registry missing (1.20.2+) | Ensure Configuration `RegistryData` sent |
| Player falls into void | Gamemode not Spectator / Y too low | `gamemode=3`, `spawn_y=400` |
| KeepAlive timeout | `read_timeout_ms` too low or network lag | Increase to 60000 for high-latency links |

---

## Related

- [Protocol Support](Protocol-Support) — packet IDs per era
- [Architecture](Architecture) — state machine
- [Schematic Support](Schematic-Support) — .schem override
- [Configuration](Configuration) — spawn/gamemode/view_distance keys