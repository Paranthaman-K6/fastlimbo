// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "server/skin.h"
#include "protocol/buffer.h"
#include "protocol/versions.h"
#include <cstring>
#include <optional>
#include <iostream>

namespace limbo::server::skin {

// --- Textures property parsing ---

// Helper: find the first property entry where name == "textures" in a properties
// array inside a Velocity MODERN payload.  The array format per verifyModern:
//   VarInt propCount, then for each: str(name), str(value), boolean(hasSig),
//   optional str(sig).
//
// Returns the value+signature bytes if found, else std::nullopt.
static std::optional<Textures>
findTexturesProperty(const uint8_t* body, size_t bodyLen)
{
    proto::Reader r(body, bodyLen);
    if (!r.ok) return std::nullopt;

    int32_t propCount = r.varInt();
    if (!r.ok || propCount < 0 || propCount > 64) return std::nullopt;

    for (int i = 0; i < propCount; ++i) {
        std::string name = r.str(255);
        if (!r.ok) return std::nullopt;
        if (name != "textures") {
            // skip value+hasSig+maybeSig
            r.str(32767); // value
            if (!r.ok) return std::nullopt;
            bool hasSig = r.boolean();
            if (!r.ok) return std::nullopt;
            if (hasSig) r.str(1024); // signature
            if (!r.ok) return std::nullopt;
            continue;
        }
        // found textures property
        std::string value = r.str(32767);
        if (!r.ok) return std::nullopt;
        bool hasSig = r.boolean();
        if (!r.ok) return std::nullopt;
        std::string signature;
        if (hasSig) {
            signature = r.str(1024);
            if (!r.ok) return std::nullopt;
        }
        return Textures{
            .value = std::move(value),
            .signature = std::move(signature),
        };
    }
    return std::nullopt;
}

std::optional<Textures> parseTexturesFromVelocityPayload(
    const std::vector<uint8_t>& payload)
{
    if (payload.size() < 32 + 1) return std::nullopt; // need at least HMAC + version varint
    // The payload after HMAC is: VarInt version || String address || UUID || username || props
    proto::Reader r(payload.data() + 32, payload.size() - 32);
    if (!r.ok) return std::nullopt;

    int32_t ver = r.varInt();
    if (!r.ok || ver < 1 || ver > 4) return std::nullopt;

    r.str(255); // address
    if (!r.ok) return std::nullopt;

    // UUID (16 bytes binary) — Reader::uuid() returns std::array<uint8_t,16>
    (void)r.uuid(); // consume UUID; we don't need it here since we only care about textures

    std::string username = r.str(16);
    if (!r.ok) return std::nullopt;

    int32_t props = r.varInt();
    if (!r.ok || props < 0 || props > 64) return std::nullopt;

    return findTexturesProperty(r.p + r.pos, r.n - r.pos);
}

// --- LoginStart trailing fields parsing ---

// Read era-appropriate trailing fields from LoginStart body after the username.
// Layout per era:
//   LEGACY_1_8:     nothing after username (body = just username)
//   V1_12:          username(16) + UUID(16) if present, else just username
//   V1_13..V1_26:  username + UUID always or optional depending on era
//
// We read conservatively: extract UUID if present, and textures if the era
// carries it.  When in doubt we leave textures empty.
//
// Sources: wiki.vg Login Start, minecraft-data protocol.json era mappings.
std::optional<LoginStartTrailing> parseLoginStartTrailing(
    const std::vector<uint8_t>& body, int pvn)
{
    proto::Era e = proto::eraForPvn(pvn);
    if (e == proto::Era::UNKNOWN) return std::nullopt;

    proto::Reader r(body);
    // username is always first: 16 bytes (or varInt length). LoginStart uses
    // a VarInt-prefixed string of max 16 chars per the protocol.
    std::string username = r.str(16);
    if (!r.ok) return std::nullopt;

    LoginStartTrailing trailing{};
    trailing.uuid.fill(0);

    switch (e) {
    case proto::Era::LEGACY_1_8:
        // PVN 47: only username, no UUID, no textures
        // nothing more to read
        break;

    case proto::Era::V1_12:
    case proto::Era::V1_13:
    case proto::Era::V1_14:
    case proto::Era::V1_15:
    case proto::Era::V1_16_0:
    case proto::Era::V1_16_2:
        // These eras carry a UUID (16 bytes binary) after the username.
        // Some older sub-eras may not always include it, but we read it when
        // the body has enough data.  If absent, uuid stays zeroed.
        if (r.remaining() >= 16) {
            trailing.uuid = r.uuid();
        }
        break;

    case proto::Era::V1_19_0:
    case proto::Era::V1_19_2:
    case proto::Era::V1_19_3:
    case proto::Era::V1_19_4:
    case proto::Era::V1_20_0_1:
    case proto::Era::V1_20_2:
    case proto::Era::V1_20_3_4:
    case proto::Era::V1_20_5_6:
    case proto::Era::V1_21_0_1:
    case proto::Era::V1_21_2_3:
    case proto::Era::V1_21_5_8:
    case proto::Era::V1_21_9_10:
    case proto::Era::V1_21_11:
    case proto::Era::V1_26_1:
    case proto::Era::V1_26_2:
    case proto::Era::V1_26_3:
        // Modern eras (1.19+): UUID is present after username, and textures
        // property MAY appear in the properties section.  We read the UUID
        // here since the LoginStart body includes it at this position.
        if (r.remaining() >= 16) {
            trailing.uuid = r.uuid();
            // textures will be extracted later from LoginPlugin response.
            // For now we leave textures empty; it will be filled from Velocity
            // forwarding payload when MODERN is active.
        }
        break;

    default:
        // Should not reach here since we check for UNKNOWN above.
        return std::nullopt;
    }

    // Attempt to extract textures from the trailing fields if present.
    // The LoginStart body after username+UUID (if any) may contain a
    // VarInt properties count followed by property entries, and one of those
    // may be the "textures" property.  We try to read it.
    size_t pos = r.pos;
    auto texturesOpt = findTexturesProperty(r.p + pos, r.n - pos);
    if (texturesOpt.has_value()) {
        trailing.textures = *texturesOpt;
    }

    return std::optional<LoginStartTrailing>(trailing);
}

// --- LoginSuccess builder ---

// Helper: write a single properties entry [name, value, hasSig?, sig?] into w.
static void writePropertyEntry(proto::Writer& w,
                               const std::string& name,
                               const std::string& value,
                               bool hasSig,
                               const std::string& signature)
{
    w.str(name);
    w.str(value);
    w.boolean(hasSig);
    if (hasSig) {
        w.str(signature);  // VarInt-prefixed string, matches r.str() reading
    }
}

std::vector<uint8_t> buildLoginSuccessBody(
    int pvn, const std::array<uint8_t, 16>& uuid,
    const std::string& username,
    const std::optional<Textures>& textures)
{
    proto::Writer w;

    // UUID: pre-1.16 uses dashed string, 1.16+ uses binary 16-byte field.
    if (pvn < 735) {
        // Pre-1.16: dashed UUID string
        char s[37];
        snprintf(s, sizeof(s),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
                 uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
        w.str(s);
    } else {
        // 1.16+: binary UUID
        w.uuid(uuid);
    }

    // Username
    w.str(username);

    // Properties section: present only when textures are available AND the era
    // supports properties (1.19+).  When textures are absent we write a zero-count
    // properties section to preserve the existing default-Steve/Alex behaviour.
    if (textures.has_value()) {
        // 1.19+ (pvn >= 759): write property count = 1, then the textures entry.
        // For eras 1.16-1.18 (735-758) we also write properties count but with
        // no entries (or just the textures entry if somehow reached).
        w.varInt(1);
        writePropertyEntry(w, "textures",
                           textures->value,
                           !textures->signature.empty(),
                           textures->signature);
    } else if (pvn >= 759) {
        // 1.19+: write zero property count so the client sees no extra properties.
        w.varInt(0);
    }
    // For 1.16-1.18 without textures: no properties slot written at all,
    // matching the original loginSuccessBody behaviour (count omitted).

    // Legacy pre-1.16 never has a properties slot.
    // 1.16-1.18 without textures: count omitted (existing behaviour).

    // Strict error handling flag (1.20.5-1.21.1 only, PVN 766/767).
    if (pvn == 766 || pvn == 767) w.boolean(false);

    // Session ID (26.2+, PVN >= 776): random per-login UUID after the profile.
    // Vanilla 26.2+ rejects LoginSuccess without it (client closes before
    // LoginAcknowledged). 775 and below must NOT include it.
    if (pvn >= 776) {
        std::array<uint8_t,16> sid;
        for (int i = 0; i < 16; ++i) sid[i] = static_cast<uint8_t>(rand() & 0xFF);
        w.uuid(sid);
    }

    return w.b;
}

// --- Play PlayerInfo skin builders ---

// Add-Player (0x0D) / Update-Info (0x0E) skin entry per era.
// We reuse the proto::Writer pattern established throughout the codebase.

// Write a skin entry: UUID(16) + GameMode(VarInt) + DisplayName + IsSkinClassic + Texture(opt)
static std::vector<uint8_t> buildSkinEntry(
    int pvn, const std::array<uint8_t, 16>& uuid,
    int gameMode, const std::string& displayName, bool isClassic,
    const std::optional<Textures>& textures)
{
    (void)pvn;
    proto::Writer w;

    // UUID (16 bytes binary) — always present in 1.13+
    w.uuid(uuid);

    // Game Mode (VarInt)
    w.varInt(gameMode);

    // Display Name: if empty, send "Player_name" default (vanilla behaviour).
    std::string dn = displayName;
    if (dn.empty()) {
        // Use the legacy default; alternatively we could send a VarInt 0 then
        // the client substitutes the username.  For simplicity we send a fixed
        // short string so the VarInt length prefix works.
        dn = "Player_name";
    }
    w.str(dn);

    // Is Skin Classic (Boolean): true = classic 64×64/64×32 Steve/Alex,
    // false = custom UUID-based skin (when textures present).
    w.boolean(isClassic);

    // Texture (String, VarInt-prefixed, base64-encoded) — present only when
    // textures are available AND isSkinClassic is true; omitted otherwise.
    // This follows the wiki.vg Player Info packet shape where the Texture field
    // is optional and only sent when a custom skin is available.
    if (textures.has_value() && isClassic) {
        w.varInt(static_cast<int32_t>(textures->value.size()));
        w.bytes(reinterpret_cast<const uint8_t*>(textures->value.data()), textures->value.size());
    }

    return w.b;
}

std::vector<uint8_t> buildPlayAddPlayerSkin(
    int pvn, const std::array<uint8_t, 16>& uuid,
    const std::string& displayName, bool isClassic,
    const std::optional<Textures>& textures)
{
    // Era gate: skin entries (0x0D / 0x0E) are defined for 1.13+ only.
    // Pre-1.13 clients connect via LEGACY_1_8 but do not have a skin entry
    // in Player Info; they just see default Steve/Alex.
    if (pvn < 393) {
        // Pre-1.13: no skin entry; return empty body so the client keeps
        // the default skin.  The caller should handle this at a higher level.
        return {};
    }

    return buildSkinEntry(pvn, uuid, 0 /* gameMode */,
                          displayName, isClassic, textures);
}

std::vector<uint8_t> buildPlayUpdatePlayerSkin(
    int pvn, const std::array<uint8_t, 16>& uuid,
    bool isClassic,
    const std::optional<Textures>& textures)
{
    // Same shape as Add-Player but for Update-Info (0x0E).  Only the changed
    // fields are included; the client merges with existing skin data.
    // We reuse buildSkinEntry with gameMode=0 (unspecified) and empty displayName
    // (no change).
    if (pvn < 393) {
        return {};
    }

    // For update, we don't change game mode or display name — just the texture
    // / classic flag.  We still need a displayName string for the VarInt prefix
    // but it will be empty (no change).
    return buildSkinEntry(pvn, uuid, 0, "", isClassic, textures);
}

} // namespace limbo::server::skin