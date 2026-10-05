# Limbo References — Consolidated Findings

Date: 2026-10-05. Primary sources take precedence (MCP/context7 low yield — see §5).

## 1. Minimal login → play packet sequence

Sources:
- wiki.vg Protocol: https://wiki.vg/Protocol
- minecraft.wiki FAQ: https://minecraft.wiki/w/Minecraft_Wiki:Projects/wiki.vg_merge/Protocol_FAQ
- minecraft.wiki packets: https://minecraft.wiki/w/Java_Edition_protocol/Packets
- PrismarineJS `minecraft-data` `protocol.json` / `loginPacket.json`: https://github.com/PrismarineJS/minecraft-data
- NanoLimbo: https://github.com/Nan1t/NanoLimbo
- PicoLimbo: https://github.com/Quozul/PicoLimbo
- LOOHP Limbo: https://github.com/LOOHP/Limbo
- Elytrium LimboAPI: https://github.com/Elytrium/LimboAPI

### Pre-1.20.2 (Handshake → Login → Play)

1. `Handshake` C→S (state 2 = login) — [wiki.vg/Protocol, protocol.json].
2. `Login Start` C→S (username [+ UUID if offline / SigData if 1.19+ chat-signing]) — [protocol.json, minecraft.wiki/Packets].
3. Optional `Set Compression` S→C (threshold, usually after this all packets compressed) — [wiki.vg/Protocol].
4. Optional `Encryption Request/Response` (online-mode only; skipped in limbo offline/velocity mode) — [Protocol FAQ].
5. Optional Velocity `Login Plugin Request/Response` (see §2) — [docs.papermc.io/velocity, VelocityServerConnection.java].
6. `Login Success` S→C (UUID + username [+ properties 1.19+]) — [protocol.json, NanoLimbo `LoginSuccess`].
7. Immediate switch to Play (no ack packet pre-1.20.2) — [Protocol FAQ, PicoLimbo `LoginSuccessPacket` → `JoinGame`].
8. Minimal Play (all impls agree — NanoLimbo `PacketsOut`, PicoLimbo `Play` handlers, LOOHP `Packets`, LimboAPI `LimboPlayer`):
   `Join Game` → `Plugin Message (brand)` → `Server Difficulty` → `Player Abilities` → `Held Item Change` → `Declare Recipes/Synchronize Player Position` (versions vary, can be minimal/empty) → `Spawn Position` → `Player Position and Look` → `Chunk Data (void)` → `Keep Alive` loop — [NanoLimbo, PicoLimbo, LOOHP Limbo, LimboAPI].

Packet IDs are version-dependent — resolve via `protocol.json` per version, do not hardcode — [PrismarineJS minecraft-data].

### 1.20.2+ (adds Configuration state)

Backwards-incompatible change in 1.20.2: new `Configuration` state between Login and Play — [minecraft.wiki/Packets, Protocol FAQ, protocol.json `configuration` section].

1. Steps 1–6 same, then `Login Acknowledged` C→S (0x03 login-bound) switches to Configuration — [wiki.vg/Protocol, LOOHP 1.20.2 support commit].
2. Configuration S→C minimal: `Plugin Message` → `Registry Data` (dimension_type / biome / chat — required or client stalls) → `Finish Configuration` — [protocol.json, LOOHP Limbo, LimboAPI 1.20.2+ branch].
3. `Finish Configuration` (ack) C→S, then switch to Play — [wiki.vg/Protocol].
4. Play sequence as above (Join Game ID shifted, e.g. 0x28/0x2B depending on version — look up in `protocol.json`) — [PrismarineJS minecraft-data].

Limbo implication: version-gate needed — `<1.20.2` skip Configuration; `>=1.20.2` implement Configuration with Registry Data + Finish handshake — [NanoLimbo fork notes, LOOHP Limbo, Elytrium LimboAPI].

## 2. Velocity modern forwarding

Sources:
- Forwarding docs: https://docs.papermc.io/velocity/player-information-forwarding
- `LoginSessionHandler.java`: https://github.com/PaperMC/Velocity/blob/dev/3.0.0/proxy/src/main/java/com/velocitypowered/proxy/connection/client/LoginSessionHandler.java
- `PlayerDataForwarding.java`: https://github.com/PaperMC/Velocity/blob/dev/3.0.0/proxy/src/main/java/com/velocitypowered/proxy/util/PlayerDataForwarding.java
- `VelocityServerConnection.java`: https://github.com/PaperMC/Velocity/blob/dev/3.0.0/proxy/src/main/java/com/velocitypowered/proxy/connection/backend/VelocityServerConnection.java
- FabricProxy-Lite `VelocityLib.java`: https://github.com/Okality/VelocityLib (as vendored in FabricProxy-Lite)

Byte layout (all multi-byte fields big-endian / VarInt+String = MC format):

1. Velocity → backend `Login Plugin Request` C-bound-login: `messageId VarInt` + `channel String = "velocity:player_info"` + empty payload — [VelocityServerConnection.java, PlayerDataForwarding.java].
2. Backend → Velocity `Login Plugin Response` S-bound-login (`0x02`): `messageId VarInt (echo)` + `successful Boolean (true)` + `data byte[]` — [LoginSessionHandler.java, minecraft.wiki/Packets#Login_Plugin_Response].
3. `data` = `HMAC-SHA256(32 bytes) || payload` where HMAC key = forwarding `secret`, message = `payload` bytes — [PlayerDataForwarding.java, VelocityLib.java `checkIntegrity`].
4. `payload` = `VERSION VarInt (1..4)` + `Address String` + `UUID (16 bytes)` + `Username String` + `Properties [...]` (`VarInt count`, each `String name`, `String value`, `Boolean signed` + optional `String signature`) — [PlayerDataForwarding.java, docs.papermc.io/velocity/player-information-forwarding].
5. Versions `1..4` incremental (newer Velocity sends highest; backend must accept 1..4, reject unknown/unsupported) — [PlayerDataForwarding `VERSION` constants, docs.papermc.io].
6. Verify with constant-time compare (`MessageDigest.isEqual(expected, actual)`, never `Arrays.equals`/early-exit) — [PlayerDataForwarding.java, VelocityLib.java].
7. Failure → disconnect; no fallback to legacy/Bungee (`BungeeGuard` separate) — [LoginSessionHandler.java, docs.papermc.io].

## 3. Chunk / void / spawn recipe

Sources: NanoLimbo, PicoLimbo, LOOHP Limbo, LimboAPI (§1) + minecraft.wiki/Packets + wiki.vg/Protocol.

- **Void recipe per era** (all send exactly one empty chunk at 0,0, client never leaves it):
  - `<1.14`: `Chunk Data` bulk / `Map Chunk` with zero sections + `Full bright` — [PicoLimbo legacy handler].
  - `1.14–1.17`: `Chunk Data (0x21/0x22)` single chunk, bitmask empty, heightmaps `MOTION_BLOCKING` empty, no entities — [NanoLimbo `EmptyChunk`].
  - `1.18+` (new world height / 3D biomes): chunk must include biome palette (single `plains` entry) + heightmaps, plus `Update Light` full-bright or client renders black — [LOOHP Limbo `EmptyChunkData`, LimboAPI `VirtualChunk`].
  - `1.20.2+`: chunk sent in Play as before, but `Registry Data` in Configuration must already have defined `dimension_type`/`biome` or chunk is rejected — [LOOHP 1.20.2+, LimboAPI].
- **`GameEvent 13` (1.20.3+)**: S→C `Game Event` with `event=13 (Start waiting for level chunks)` is **required** before/with chunk or client hangs on loading screen — added in 1.20.3; all maintained limbos backported it — [minecraft.wiki/Packets#Game_Event, LOOHP Limbo commit, LimboAPI commit, NanoLimbo issue].
- **Spawn**: `Spawn Position` + `Player Position and Look` at fixed high Y (`Y=400`, void below, no fall damage in limbo gamemode) — convention shared by NanoLimbo/PicoLimbo/LOOHP/LimboAPI defaults.
- **Gamemode**: `Spectator (3)` in `Join Game` + `Player Abilities (fly, invulnerable)` so player cannot move/interact/fall — [NanoLimbo `GameMode.SPECTATOR`, LOOHP default].
- **KeepAlive**: S→C `Keep Alive` every ~10–15 s (NanoLimbo 10 s scheduler; vanilla 15 s) and kick on timeout; also answers `Ping/Pong` + `Status` in status state — [NanoLimbo `KeepAliveTask`, PicoLimbo loop, wiki.vg/Protocol].

## 4. Schematic scope decision

Spec sources:
- Sponge Schematic Spec v2/v3: https://github.com/SpongePowered/Schematic-Specification
- minecraft.wiki Schematic file format: https://minecraft.wiki/w/Schematic_file_format
- Litematica format: https://github.com/maruohon/litematica (wiki: `litematic` NBT structure)

Decision (already agreed, record here):

- **First: void + Sponge `.schem` v2.** Modern, documented (v2/v3 spec), zlib-gzip NBT, palette + `BlockData` varints, `Offset`, `Size`, supported by WE/FAWE export. Single reader covers current builder output — [Sponge Schematic-Specification v2].
- **Defer: `.schematic` (MCEdit legacy), `.litematic`, `.mca`.** Reasons: `.schematic` legacy IDs + ambiguous extended-IDs (`AddBlocks`) — [minecraft.wiki Schematic file format]; `.litematic` needs region-entities/pending-ticks handling + separate spec drift — [Litematica format]; `.mca` needs region/chunk/heightmap/light reconstruction, out of limbo-minimal scope.
- Staged plan: (1) void-only limbo, (2) `.schem` v2 paste at spawn, (3) revisit deferred formats only on demand.

## 5. MCP / context7 check 2026-10-05

- Queried Context7 IDs `/papermc/velocity` and `/prismarinejs/node-minecraft-protocol`: low yield (stale/partial snippets, missing Configuration-era packets and Velocity forwarding versions).
- Decision: **primary sources in §§1–4 take precedence**; do not treat context7 output as authoritative for packet IDs or forwarding layout. Re-check only if upstream docs move.
