#pragma once
#include <string>
#include "config.h"

namespace limbo::server {

// Blocking per-connection handler. Returns after close.
void handleConnection(int fd, std::string peerIp, const Config& cfg, int onlineCount);

}  // namespace limbo::server
