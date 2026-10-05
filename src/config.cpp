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
  if (!f) return c;
  std::string line;
  auto set = [&](const std::string& k, const std::string& v) {
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
  };
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    set(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
  }
  return c;
}

}  // namespace limbo
