#pragma once
// Velocity modern forwarding verification (HMAC-SHA256, constant-time compare).
// Layout: HMAC(32) + VarInt version(1..4) + String address + UUID(16) + String username
//         + VarInt propCount + props(name,value,hasSig[,sig]) — see research §2.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace limbo::security {

struct ForwardedProfile {
  std::string address;
  std::array<uint8_t, 16> uuid{};
  std::string username;
};

// Returns true + fills profile on valid signature. Constant-time HMAC compare.
// secret: raw UTF-8 bytes of velocity.toml forwarding-secret.
bool verifyModern(const std::vector<uint8_t>& payload, const std::string& secret,
                  ForwardedProfile& out, std::string& err);

// Build LoginPluginRequest body for channel "velocity:player_info" (1 byte version=1 max).
std::vector<uint8_t> pluginRequestBody();

// Helpers exposed for tests.
std::vector<uint8_t> hmacSha256(const std::string& key, const uint8_t* msg, size_t len);
bool constantTimeEq(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);

}  // namespace limbo::security
