// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once
#include <string>
#include "config.h"

namespace limbo::server {

// Blocking per-connection handler. Returns after close.
void handleConnection(int fd, std::string peerIp, const Config& cfg, int onlineCount);

}  // namespace limbo::server
