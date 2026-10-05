#pragma once
// Play packet body builders (workstream A).
// All functions return packet BODIES only — no packet id, no length prefix.
// The lead frames with proto::framePacket(proto::playIds(pvn).<field>, body).
// IDs are never hardcoded here; the lead applies playIds(pvn) at framing time.
#include <cstdint>
#include <string>
#include <vector>

namespace limbo::play {

// Join Game body. Pre-1.16: entityId/gamemode/dimension legacy fields.
// 1.16+: minimal with empty worldNames + zero codec placeholder.
// TODO(B): inject real registry codec blob for 1.16+.
std::vector<uint8_t> joinGameBody(int pvn, int entityId);

// Player Abilities body (flags + flying/walking speed).
std::vector<uint8_t> abilitiesBody(int pvn);

// Synchronize Player Position / Player Position and Look body.
// 1.21.2+ uses teleportId-first layout.
std::vector<uint8_t> positionBody(int pvn, double x, double y, double z,
                                  float yaw, float pitch, int teleportId);

// Game Event body (event, value). Event 13 = start waiting for level chunks.
std::vector<uint8_t> gameEventBody(int pvn, int event, float value);

// Center Chunk body (chunkX, chunkZ). Empty for pre-1.14.
std::vector<uint8_t> centerChunkBody(int pvn, int chunkX, int chunkZ);

// Chunk Batch Start body (empty) and Chunk Batch Finished body (batch size).
// Only used on 1.20.2+ (batchStart/batchFinished IDs from playIds).
std::vector<uint8_t> batchStartBody(int pvn);
std::vector<uint8_t> batchFinishedBody(int pvn, int batchSize);

}  // namespace limbo::play
