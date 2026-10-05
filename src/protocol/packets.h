// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once
// Small packet builders. Bodies only — framing via framePacket().
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace limbo::packets {

std::string jsonEscape(const std::string& s);

// Status response JSON: version(name,protocol) + players(max,online) + description.
std::vector<uint8_t> statusResponseBody(const std::string& versionName, int protocol,
                                        int maxPlayers, int online, const std::string& motd);

// Login Disconnect body (login state 0x00): String JSON pre-1.20.3, NBT afterwards.
std::vector<uint8_t> loginDisconnectBody(int pvn, const std::string& message);

// Login Success body (login state 0x02): UUID(16) + username [+ properties count 0 for 1.19+].
std::vector<uint8_t> loginSuccessBody(int pvn, const std::array<uint8_t,16>& uuid,
                                      const std::string& username);

// Login PluginRequest body container: msgId + channel + data.
std::vector<uint8_t> loginPluginRequestBody(int32_t msgId, const std::string& channel,
                                            const std::vector<uint8_t>& data);

// Play Disconnect body: String JSON pre-1.20.3, NBT TextComponent 1.20.3+.
std::vector<uint8_t> playDisconnectBody(int pvn, const std::string& message);

// Configuration Disconnect body (config state): id differs but body same shape as play.
std::vector<uint8_t> configDisconnectBody(int pvn, const std::string& message);

}  // namespace limbo::packets
