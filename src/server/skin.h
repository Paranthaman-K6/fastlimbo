// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "protocol/buffer.h"
#include "security/velocity.h"

namespace limbo::server::skin {

// --- Textures property, byte-exact ---

// The 'textures' property extracted from Velocity MODERN forwarding payload
// (or from LoginStart trailing fields).  value is base64-encoded JSON; signature
// is base64-encoded and may be empty when absent.
struct Textures {
    std::string value;   // base64, never empty when present
    std::string signature; // base64, may be empty (unsigned)
};

// Parse the 'textures' property (name == "textures") from a Velocity MODERN
// LoginPluginResponse payload.  Returns {} if the payload does not contain a
// textures entry, or if parsing fails (caller must not crash).
// The payload format is: HMAC(32) || VarInt version || String address ||
// UUID(16) || String username || VarInt propCount ++ [name, value, hasSig? sig]
//
// Source: wiki.vg + Velocity docs — properties array entries have
//   {name: String, value: String (base64), signed: Boolean, signature: ByteArray}.
//
std::optional<Textures> parseTexturesFromVelocityPayload(
    const std::vector<uint8_t>& payload);

// Parse trailing LoginStart fields from a LoginStart body.
// Reads era-appropriate fields after the username string.
//   • LEGACY_1_8 (PVN 47):   username only — no UUID, no textures.
//   • V1_12  (PVN 335/338/340): username + UUID (16 bytes binary) if present,
//     otherwise no UUID, no textures.
//   • V1_13  (PVN 393–578):   username + UUID (16 bytes binary) always present.
//   • V1_14  (PVN 477–498):   same as V1_13.
//   • V1_15  (PVN 573–578):   same as V1_13.
//   • V1_16_0/2 (PVN 735–754): username + UUID + optional textures property.
//   • V1_19_0..4 (PVN 759–762): username + UUID + textures property may appear.
//   • V1_20_0_1..5_6 (PVN 763–766): username + UUID + textures property may appear.
//   • V1_21_* (PVN 767–774):  username + UUID + textures property may appear.
//   • V1_26_* (PVN 775–777):  username + UUID + textures property may appear.
// Returns {} if the body is too short or the era does not carry the field.
// The UUID field is filled in via offlineUuid() when absent but the era expects it;
// callers may re‑generate it if desired.
struct LoginStartTrailing {
    std::array<uint8_t, 16> uuid{};
    std::optional<Textures> textures;
};

std::optional<LoginStartTrailing> parseLoginStartTrailing(
    const std::vector<uint8_t>& body, int pvn);

// --- LoginSuccess builders ---

// Build the LoginSuccess (state 0x02) body.
//
// Per-era behavior (cite source in comments):
//   • Pre‑1.16 (pvn < 735):  UUID sent as dashed decimal string per
//     packets.cpp::loginSuccessBody.  No properties slot is written.
//   • 1.16 – 1.18 (735 ≤ pvn ≤ 758): UUID as binary 16‑byte field, no
//     properties slot written (properties count omitted);
//   • 1.19+ (pvn ≥ 759):     UUID as binary 16‑byte field, followed by
//     VarInt property count, then properties entries [{name, value, signed?, sig?}].
//     If textures are available the 'textures' property is included;
//     otherwise a zero‑count properties section is written (matching the
//     existing default behaviour);
//   • 1.20.5 – 1.21.1 (pvn == 766 || pvn == 767):  same as 1.19+, but an
//     extra boolean false is appended for strict error handling (the existing
//     loginSuccessBody already emits this boolean for these PVNs).
//
// Reuses proto::Writer.  No Mojang API requests, no network, offline‑safe.
//
// Source: packets.cpp::loginSuccessBody original + minecraft-data protocol.json
// era gates verified against versions.h eraForPvn().
//
std::vector<uint8_t> buildLoginSuccessBody(
    int pvn, const std::array<uint8_t, 16>& uuid,
    const std::string& username,
    const std::optional<Textures>& textures);

// --- Play PlayerInfo builders (Add-Player / Update-Info) ---

// Packet IDs (cite source from minecraft-data protocol.json / wiki.vg):
//   0x0D = Add Player Info (clientbound)
//   0x0E = Update Player Info (clientbound)
// These IDs are stable across all eras 1.13+; pre‑1.13 behaviour is undefined
// for limbo's offline‑only void world.

// Build the Add-Player (0x0D) skin entry.
//
// Era‑gated skin entry:
//   • UUID (16 bytes binary) — always present;
//   • Game Mode (VarInt, 0–3);
//   • Display Name (String, VarInt‑prefixed); empty → default "Player_name";
//   • Is Skin Classic (Boolean) — true when the skin uses the classic
//     Steve/Alex 64×64 / 64×32 format, false for custom UUID‑based skins;
//   • Texture (String, VarInt‑prefixed, base64‑encoded) — present only when
//     textures are available AND isSkinClassic is true; omitted otherwise.
//
// Returns an empty body (no crash) when textures are missing; the caller may
// substitute the default Steve/Alex skin.
//
// Source: wiki.vg Protocol — Player Info (0x0D), verified against
// PrismarineJS minecraft-data protocol.json per-era packet shapes.
std::vector<uint8_t> buildPlayAddPlayerSkin(
    int pvn, const std::array<uint8_t, 16>& uuid,
    const std::string& displayName, bool isClassic,
    const std::optional<Textures>& textures);

// Build the Update-Info (0x0E) skin entry.
//
// Same shape as Add-Player (0x0D) but only the fields that changed are
// included; all other fields are unchanged on the client.
//
// Source: wiki.vg Protocol — Player Info (0x0E), same era gates as 0x0D.
std::vector<uint8_t> buildPlayUpdatePlayerSkin(
    int pvn, const std::array<uint8_t, 16>& uuid,
    bool isClassic,
    const std::optional<Textures>& textures);

} // namespace limbo::server::skin