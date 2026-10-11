// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once
#include <atomic>
#include <string>
#include "config.h"
#include "server/version_policy.h"

namespace limbo::server {

// Version allowlist, set once at startup from allowed_versions.
void setVersionPolicy(const std::string& spec);
const Policy& versionPolicy();

// Blocking per-connection handler. Returns after close.
// `online` is the LIVE player counter (play sessions only; status pings never
// touch it). Incremented once LoginSuccess is sent, decremented on leave.
void handleConnection(int fd, std::string peerIp, const Config& cfg,
                      std::atomic<int>& online);

}  // namespace limbo::server
