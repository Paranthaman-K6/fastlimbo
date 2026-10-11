// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#pragma once

#include <set>
#include <string>
#include <vector>

namespace limbo::server {

struct Policy {
  std::set<int> allowed;
  std::vector<std::string> warnings;
};

// Parse an allowed_versions spec: comma-separated items, whitespace tolerated.
// Items: "all" | decimal PVN | "low-high" range | release label (e.g. "1.21.1").
// Never throws, never exits: problems become strings in Policy::warnings.
Policy parseAllowedVersions(const std::string& spec);

bool isAllowed(const Policy&, int pvn);

std::string summarize(const Policy&);

}  // namespace limbo::server
