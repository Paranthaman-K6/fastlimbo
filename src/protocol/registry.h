// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once
// Configuration-state registry payloads (1.20.2+/PVN 764+).
//
// Bodies only (no packet framing) — callers wrap with proto::framePacket(id, body).
// Packet IDs cross-checked against minecraft-data protocol.json (1.20.2/1.20.3/
// 1.20.5/1.21.1/1.21.4) and NanoLimbo State.java. See docs/registry-notes.md.
#include <cstdint>
#include <vector>

namespace limbo::registry {

// Clientbound Configuration packet ids for one PVN.
// registryData | finish | keepAlive | disconnect
struct ConfigIds {
  int registryData;
  int finish;
  int keepAlive;
  int disconnect;
};

ConfigIds configIds(int pvn);

// Clientbound Update Tags id (0 = era has no Configuration state / unsupported).
int configTagsId(int pvn);
// Clientbound + serverbound Select Known Packs ids (1.20.5+/766+ only, else 0).
int configKnownPacksId(int pvn);
int configKnownPacksServerboundId(int pvn);
// Serverbound Finish Configuration ack id (clientbound id differs).
int configFinishAckId(int pvn);

// Registry Data bodies, one entry per returned element:
//   pvn <  766 (1.20.2/1.20.4): single anonymous-root NBT compound holding the
//       full codec {registry name -> {type, value:[{name,id,element}...]}}.
//   pvn >= 766 (1.20.5+): one body per registry: String registry id +
//       VarInt count + {String entry key, Boolean present, anonymous NBT element}.
// v1 scope: dimension_type (overworld) only — TODO expand to worldgen/biome
// (plains) + chat_type + damage_type from minecraft-data loginPacket.json.
std::vector<std::vector<uint8_t>> buildRegistryBlobs(int pvn);

// Finish Configuration body: empty.
std::vector<uint8_t> buildFinish(int pvn);

// Keep Alive body: i64 id (clientbound and serverbound share the layout).
std::vector<uint8_t> buildKeepAlive(int pvn, int64_t keepAliveId);

}  // namespace limbo::registry
