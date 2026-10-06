// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once
#include <string>

namespace limbo {

struct Config {
  std::string bind = "0.0.0.0";
  int port = 25566;
  int maxPlayers = 100;
  std::string versionName = "Limbo-C++";
  std::string motd = "Limbo-C++ void";
  int protocolMax = 776;
  std::string forwarding = "NONE";  // NONE | MODERN
  std::string forwardingSecret;
  int readTimeoutMs = 30000;
  int maxPacketBytes = 8192;
  std::string schematicPath;  // "" = void-only; else .schem v2 spawn paste (1.18+ render)
};

Config loadConfig(const std::string& path);

}  // namespace limbo
