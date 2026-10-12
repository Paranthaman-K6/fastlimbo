// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include <cstdio>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include "config.h"
#include "protocol/versions.h"
#include "server/connection.h"
#include "server/tcp_server.h"
#include "server/version_policy.h"
#include "world/void_chunk.h"

namespace {
// Config lives next to the binary: ./limbo.properties beside build/limbo wins,
// then the repo layout, then CWD. Explicit --config always wins.
bool fileExists(const std::string& p) {
  struct stat st{};
  return ::stat(p.c_str(), &st) == 0;
}
std::string dirName(const std::string& p) {
  auto s = p.find_last_of('/');
  return s == std::string::npos ? "." : p.substr(0, s);
}
std::string exeDir(char** argv) {
  char buf[4096] = {};
  ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  std::string exe = n > 0 ? std::string(buf, n) : argv[0];
  return dirName(exe);
}
// Resolve a relative asset (schematic_path) against the config file's folder
// first, then the binary folder, then CWD. Absolute paths pass through.
std::string resolveAsset(const std::string& v, const std::string& cfgDir,
                         const std::string& binDir) {
  if (v.empty() || v[0] == '/') return v;
  if (!cfgDir.empty() && cfgDir != "." && fileExists(cfgDir + "/" + v))
    return cfgDir + "/" + v;
  if (fileExists(v)) return v;
  if (fileExists(binDir + "/" + v)) return binDir + "/" + v;
  if (fileExists(binDir + "/../" + v)) return binDir + "/../" + v;
  return v;  // loader reports the miss with this (void fallback) path
}
void logStartup(const std::string& cfgPath, const limbo::Config& cfg) {
  printf("limbo-c++ starting (build %s %s)\n", __DATE__, __TIME__);
  printf("[config] file=%s\n", cfgPath.c_str());
  printf("[config] bind=%s port=%d max_players=%d\n", cfg.bind.c_str(), cfg.port,
         cfg.maxPlayers);
  printf("[config] version_name=%s motd=%s\n", cfg.versionName.c_str(), cfg.motd.c_str());
  printf("[config] forwarding=%s secret=%s\n", cfg.forwarding.c_str(),
         cfg.forwardingSecret.empty() ? "(unset)" : "(set)");
  printf("[config] read_timeout_ms=%d max_packet_bytes=%d debug_level=%s\n",
         cfg.readTimeoutMs, cfg.maxPacketBytes,
         cfg.debugLevel >= 0 ? (cfg.debugLevel == 0 ? "error" : cfg.debugLevel == 1 ? "info" : cfg.debugLevel == 2 ? "debug" : "trace")
                             : (cfg.debugLog ? "debug (via debug_log=true)" : "info (default)"));
  printf("[config] allowed_versions=%s\n", cfg.allowedVersions.c_str());
  printf("[config] freeze-player=%s\n", cfg.freezePlayer ? "true" : "false");
  if (cfg.spawnAutoY)
    printf("[config] spawn=%.2f,auto,%.2f\n", cfg.spawnX, cfg.spawnZ);
  else
    printf("[config] spawn=%.2f,%.2f,%.2f\n", cfg.spawnX, cfg.spawnY, cfg.spawnZ);
  printf("[config] schematic_path=%s\n",
         cfg.schematicPath.empty() ? "(void-only)" : cfg.schematicPath.c_str());
  printf("[config] supported protocols: 47 (1.8.x) .. %d (%s)\n",
         limbo::proto::maxSupportedPvn(),
         limbo::proto::versionName(limbo::proto::maxSupportedPvn()));
  for (const auto& w : cfg.warnings) printf("[config] WARN: %s\n", w.c_str());
  fflush(stdout);
}
}  // namespace

int main(int argc, char** argv) {
  std::string binDir = exeDir(argv);
  std::string cfgPath;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if ((a == "--config" || a == "-c") && i + 1 < argc) cfgPath = argv[++i];
  }
  if (cfgPath.empty()) {
    // Same folder as the binary wins (e.g. build/limbo.properties),
    // then repo layout, then CWD-relative fallback.
    const std::string cands[] = {
        binDir + "/limbo.properties",
        binDir + "/../config/limbo.properties",
        "config/limbo.properties",
    };
    for (auto& c : cands) {
      if (fileExists(c)) { cfgPath = c; break; }
    }
    if (cfgPath.empty()) cfgPath = "config/limbo.properties";  // warned default
  }
  limbo::Config cfg = limbo::loadConfig(cfgPath);
  logStartup(cfgPath, cfg);
  limbo::server::setVersionPolicy(cfg.allowedVersions);
  {
    const auto& pol = limbo::server::versionPolicy();
    printf("[config] allowed_versions: %s\n", limbo::server::summarize(pol).c_str());
    for (const auto& w : pol.warnings) printf("[config] WARN: %s\n", w.c_str());
    fflush(stdout);
  }
  if (cfg.forwarding == "MODERN" && cfg.forwardingSecret.empty()) {
    fprintf(stderr, "[config] FATAL: forwarding=MODERN requires forwarding_secret\n");
    return 1;
  }
  if (!cfg.schematicPath.empty())
    cfg.schematicPath = resolveAsset(cfg.schematicPath, dirName(cfgPath), binDir);
  limbo::world::setSpawnSchematic(cfg.schematicPath);
  std::string schemErr;
  if (!limbo::world::loadSpawnSchematic(schemErr)) {
    printf("[world] schematic: %s (void fallback)\n", schemErr.c_str());
  } else if (!cfg.schematicPath.empty()) {
    printf("[world] schematic loaded: %s\n", cfg.schematicPath.c_str());
    for (const auto& dl : limbo::world::describeSpawnSchematic())
      printf("[world] schematic %s\n", dl.c_str());
  } else {
    printf("[world] void-only (no schematic_path)\n");
  }
  fflush(stdout);
  return limbo::server::run(cfg);
}
