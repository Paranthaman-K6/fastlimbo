#pragma once
// Void chunk payload builder + optional .schem spawn paste.
// Bodies only — excludes packet id and length prefix. The lead frames with
// proto::framePacket(proto::playIds(pvn).chunk, body) at the call site.
// Era-gated per minecraft-data protocol.json (verified 2026-10-05).
// Paste: on 1.18+ (paletted sections, stone global id 1 stable since 1.13),
// non-air schem cells render as stone (shape-preserving; full palette mapping
// deferred — see docs/ARCHITECTURE.md). Older eras always void.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace limbo::schem {
struct Schem;
}

namespace limbo::world {

struct PasteCtx {
  const schem::Schem* schem = nullptr;
  int baseX = 0;  // world coords of schem minimum corner
  int baseY = 0;
  int baseZ = 0;
};

// Build a chunk body for (chunkX, chunkZ); with ctx.schem set (1.18+ only),
// cells covered by non-air schematic blocks render as stone.
std::vector<uint8_t> buildChunkBody(int pvn, int chunkX, int chunkZ,
                                    const PasteCtx& ctx);

// Legacy entry point: pure void (no paste). Used by unit tests.
std::vector<uint8_t> buildVoidChunkBody(int pvn, int chunkX, int chunkZ);

// Spawn schematic selected at startup (config schematic_path). Call once from
// main before serving; loadSpawnSchematic parses it (void fallback on error).
void setSpawnSchematic(const std::string& path);
bool loadSpawnSchematic(std::string& err);  // true when void-only or loaded
std::optional<PasteCtx> spawnPaste();       // paste ctx for chunk (0,0), if any

}  // namespace limbo::world
