#include <cstdio>
#include <string>
#include "config.h"
#include "server/tcp_server.h"
#include "world/void_chunk.h"

int main(int argc, char** argv) {
  std::string cfgPath = "config/limbo.properties";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if ((a == "--config" || a == "-c") && i + 1 < argc) cfgPath = argv[++i];
  }
  limbo::Config cfg = limbo::loadConfig(cfgPath);
  if (cfg.forwarding == "MODERN" && cfg.forwardingSecret.empty()) {
    fprintf(stderr, "forwarding=MODERN requires forwarding_secret\n");
    return 1;
  }
  limbo::world::setSpawnSchematic(cfg.schematicPath);
  std::string schemErr;
  if (!limbo::world::loadSpawnSchematic(schemErr)) {
    fprintf(stderr, "schematic: %s (void fallback)\n", schemErr.c_str());
  } else if (!cfg.schematicPath.empty()) {
    printf("schematic loaded: %s\n", cfg.schematicPath.c_str());
  }
  return limbo::server::run(cfg);
}
