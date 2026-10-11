// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "config.h"
#include <fstream>

namespace limbo {

static std::string trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

Config loadConfig(const std::string& path) {
  Config c;
  std::ifstream f(path);
  if (!f) {
    c.warnings.push_back("config file not found: " + path + " (using defaults)");
    return c;
  }
  std::string line;
  int lineno = 0;
  auto set = [&](const std::string& k, const std::string& v) {
    try {
      if (k == "bind") c.bind = v;
      else if (k == "port") c.port = std::stoi(v);
      else if (k == "max_players") c.maxPlayers = std::stoi(v);
      else if (k == "version_name") c.versionName = v;
      else if (k == "motd") c.motd = v;
      else if (k == "protocol_max") c.protocolMax = std::stoi(v);
      else if (k == "forwarding") c.forwarding = v;
      else if (k == "forwarding_secret") c.forwardingSecret = v;
      else if (k == "read_timeout_ms") c.readTimeoutMs = std::stoi(v);
      else if (k == "max_packet_bytes") c.maxPacketBytes = std::stoi(v);
      else if (k == "schematic_path") c.schematicPath = v;
      else if (k == "allowed_versions") c.allowedVersions = v;
      else if (k == "debug_log") c.debugLog = (v == "1" || v == "true" || v == "yes");
      else if (k == "freeze-player" || k == "freeze_player") c.freezePlayer = (v == "1" || v == "true" || v == "yes");
      else if (k == "chunk_radius" || k == "chunk-radius") c.chunkRadius = std::stoi(v);
      else if (k == "spawn_x" || k == "spawn-x") c.spawnX = std::stod(v);
      else if (k == "spawn_z" || k == "spawn-z") c.spawnZ = std::stod(v);
      else if (k == "spawn_y" || k == "spawn-y") {
        std::string t = v;
        for (auto& ch : t) ch = static_cast<char>(tolower(ch));
        if (t.empty() || t == "auto") c.spawnAutoY = true;
        else { c.spawnY = std::stod(v); c.spawnAutoY = false; }
      }
      else if (k == "debug_level") {
        std::string t = v;
        for (auto& ch : t) ch = static_cast<char>(tolower(ch));
        if (t == "error" || t == "0") c.debugLevel = 0;
        else if (t == "info" || t == "1") c.debugLevel = 1;
        else if (t == "debug" || t == "2") c.debugLevel = 2;
        else if (t == "trace" || t == "3") c.debugLevel = 3;
        else c.warnings.push_back("line " + std::to_string(lineno) + ": bad debug_level '" + v + "' (want error|info|debug|trace), ignored");
      }
      else c.warnings.push_back("line " + std::to_string(lineno) + ": unknown key '" + k + "' ignored");
    } catch (...) {
      c.warnings.push_back("line " + std::to_string(lineno) + ": bad value for '" + k + "' ignored");
    }
  };
  while (std::getline(f, line)) {
    ++lineno;
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      c.warnings.push_back("line " + std::to_string(lineno) + ": no '=' ignored");
      continue;
    }
    set(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
  }
  // Sanity clamps (logged, never silent).
  if (c.port < 1 || c.port > 65535) {
    c.warnings.push_back("port out of range, reset to 25566");
    c.port = 25566;
  }
  if (c.maxPlayers < 1) {
    c.warnings.push_back("max_players < 1, reset to 100");
    c.maxPlayers = 100;
  }
  if (c.forwarding != "NONE" && c.forwarding != "MODERN") {
    c.warnings.push_back("forwarding must be NONE|MODERN, reset to NONE");
    c.forwarding = "NONE";
  }
  if (c.chunkRadius < 0 || c.chunkRadius > 4) {
    c.warnings.push_back("chunk_radius out of range 0..4, reset to 1");
    c.chunkRadius = 1;
  }
  return c;
}

}  // namespace limbo
