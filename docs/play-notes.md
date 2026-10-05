# Play burst notes — workstream A (void + keepalive)

Date: 2026-10-05. Body builders only; the lead frames with
`proto::framePacket(proto::playIds(pvn).<field>, body)`. No IDs hardcoded in
`play.cpp` / `void_chunk.cpp` — IDs come from `playIds(pvn)` at framing time.

## Files

- `src/world/void_chunk.h/.cpp` — `buildVoidChunkBody(pvn, chunkX, chunkZ)`
- `src/protocol/play.h/.cpp` — `joinGameBody`, `abilitiesBody`, `positionBody`,
  `gameEventBody`, `centerChunkBody`
- `tests/test_void_chunk.cpp`, `tests/test_play.cpp`

## Void chunk body (buildVoidChunkBody)

Excludes packet id + length. Era-gated:

- **LEGACY_1_8 (47)** — Map Chunk: `i32 x, i32 z, boolean true, varInt 0, varInt 0`
- **V1_12 (335-340)** — Chunk Data: `i32 x, i32 z, boolean false, varInt 0, varInt 0, varInt 0`
- **V1_13_1_15 (393-578)** — same as V1_12
- **1.16+ (default)** — `i32 x, i32 z`, heightmaps NBT (TAG_Compound + TAG_Long_Array
  "MOTION_BLOCKING" i32(37) + 37×i64(0) + TAG_End), `varInt 0` (no sections),
  1024×`varInt 0` (biomes, single plains idx 0), `varInt 0` (block entities)

Heightmap NBT is a stub shape (pre-1.21.5). Refine when B injects real codec.

## Play bodies (play.cpp)

### joinGameBody(pvn, entityId)

- **LEGACY_1_8**: `i32 entityId, u8 gamemode(3), u8 dimension(0), u8 difficulty(2), u8 maxPlayers(100), str "default", boolean false`
- **V1_12**: `i32, u8(3), i32(0), u8(2), u8(100), str "default", boolean false`
- **V1_13_1_15**: `i32, u8(3), i32(0), i64(0), u8(100), str "default", varInt(2), boolean false`
- **1.16+**: `i32 entityId, u8 gamemode(3), u8 prevGamemode(0), varInt(0) worldNames, NBT empty compound (codec stub), NBT empty compound (dimension stub), i64(0), u8(100), varInt(2), boolean false, boolean true`
  - TODO(B): inject real registry codec blob for 1.16+ (worldNames + codec + dimension)

### abilitiesBody(pvn)

All eras: `u8 flags(0x07 = invulnerable+flying+canfly), f32 flySpeed(0.05), f32 walkSpeed(0.1)`

### positionBody(pvn, x, y, z, yaw, pitch, teleportId)

- **<1.21.2**: `f64 x, f64 y, f64 z, f32 yaw, f32 pitch, u8 flags(0), varInt teleportId`
- **1.21.2+**: `varInt teleportId, f64 x, f64 y, f64 z, f32 yaw, f32 pitch, u8 flags(0)`

### gameEventBody(pvn, event, value)

All eras: `u8 event, f32 value`. Event 13 = start waiting for level chunks.
Only send when `needsGameEvent13(pvn)` (pvn >= 765).

### centerChunkBody(pvn, chunkX, chunkZ)

Empty for pvn < 760 (1.19.2+). Otherwise: `varInt chunkX, varInt chunkZ`.

## Merge snippet for connection.cpp (function signatures only)

```cpp
// Call order: JoinGame -> Abilities -> Position -> GameEvent13 -> Center -> Chunk -> KeepAlive loop
auto jg = limbo::play::joinGameBody(pvn, entityId);
writeAll(fd, proto::framePacket(proto::playIds(pvn).joinGame, jg));

auto ab = limbo::play::abilitiesBody(pvn);
writeAll(fd, proto::framePacket(proto::playIds(pvn).abilities, ab));

auto pos = limbo::play::positionBody(pvn, spawnX, spawnY, spawnZ, 0.0f, 0.0f, teleportId);
writeAll(fd, proto::framePacket(proto::playIds(pvn).position, pos));

if (proto::needsGameEvent13(pvn)) {
  auto ge = limbo::play::gameEventBody(pvn, 13, 0.0f);
  writeAll(fd, proto::framePacket(proto::playIds(pvn).gameEvent, ge));
}

auto cc = limbo::play::centerChunkBody(pvn, 0, 0);
if (!cc.empty()) writeAll(fd, proto::framePacket(proto::playIds(pvn).centerChunk, cc));

auto chunk = limbo::world::buildVoidChunkBody(pvn, 0, 0);
writeAll(fd, proto::framePacket(proto::playIds(pvn).chunk, chunk));

// KeepAlive loop: send every ~10-15s, kick on timeout.
// Also: answer teleport-confirm (client sends teleport id back), ignore movement packets.
```

## Compatibility notes

Covered PVNs (via era helpers): 47, 340, 754, 762, 763, 764, 766, 767 (and all
eras in between). Pre-1.16 JoinGame is legacy-shaped; 1.16+ uses minimal
stub (B injects real codec later). GameEvent13 gated by `needsGameEvent13`.
Position layout switches at 1.21.2 (teleportId-first). CenterChunk empty
pre-1.19.2.
