// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once
#include <string>
#include <vector>

namespace limbo {

struct Config {
  std::string bind = "0.0.0.0";
  int port = 25566;
  int maxPlayers = 100;
  std::string versionName = "Limbo-C++";
  std::string motd = "Limbo-C++ void";
  int protocolMax = 774;  // legacy cap, informational (acceptance uses version tables)
  std::string forwarding = "NONE";  // NONE | MODERN
  std::string forwardingSecret;     // never printed; startup shows set/unset only
  int readTimeoutMs = 30000;
  int maxPacketBytes = 8192;
  std::string schematicPath;  // "" = void-only; else .schem v2 spawn paste (1.18+ render)
  std::string allowedVersions = "all";  // version allowlist spec (hook for policy)
  bool debugLog = false;                // legacy alias: true maps to DEBUG when debug_level unset
  int debugLevel = -1;  // -1 = unset (use debugLog compat); 0=ERROR,1=INFO,2=DEBUG,3=TRACE
  bool freezePlayer = true;  // freeze-player: teleport-back on movement (false = walkable void)
  int chunkRadius = 1;       // chunk-radius: half-extent of login burst around spawn (0..4)
  double spawnX = 8.5;       // spawn-x: spawn/freeze X (block center)
  double spawnZ = 8.5;       // spawn-z: spawn/freeze Z (block center)
  double spawnY = 0;         // spawn-y value when spawnAutoY is false
  bool spawnAutoY = true;    // spawn-y empty/auto: feet on platform top, else 400.0 void
  std::vector<std::string> warnings;    // unknown keys / clamped values (startup log)

  // Effective level: explicit debug_level wins, else debugLog compat, else INFO.
  int level() const {
    if (debugLevel >= 0) return debugLevel;
    return debugLog ? 2 : 1;
  }
};

Config loadConfig(const std::string& path);

}  // namespace limbo
