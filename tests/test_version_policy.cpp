// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include <cassert>
#include <cstdio>

#include "server/version_policy.h"

int main() {
  using namespace limbo::server;

  // "all" (empty, whitespace, explicit) covers every known supported PVN.
  for (const char* spec : {"", "   ", "all", " all "}) {
    Policy p = parseAllowedVersions(spec);
    assert(p.warnings.empty());
    assert(isAllowed(p, 47));
    assert(isAllowed(p, 340));
    assert(isAllowed(p, 763));
    assert(isAllowed(p, 767));
    assert(isAllowed(p, 775));
    assert(isAllowed(p, 776));
    assert(isAllowed(p, 777));
    assert(!isAllowed(p, 99999));
  }

  // Label, range, and number parsing.
  Policy p = parseAllowedVersions("1.21.1, 759-762, 763");
  assert(p.warnings.empty());
  assert(isAllowed(p, 767));   // from "1.21.1"
  assert(isAllowed(p, 759) && isAllowed(p, 760) && isAllowed(p, 761) && isAllowed(p, 762));
  assert(isAllowed(p, 763));
  assert(!isAllowed(p, 758));
  assert(!isAllowed(p, 47));

  // Unknown label: warning, valid parts still apply.
  Policy u = parseAllowedVersions("1.12.2, banana, 767");
  assert(u.warnings.size() == 1);
  assert(isAllowed(u, 340));
  assert(isAllowed(u, 767));
  assert(!isAllowed(u, 763));

  // Snapshot PVN (no known era) is rejected even if listed.
  Policy snap = parseAllowedVersions("100, 999");
  assert(!isAllowed(snap, 100));
  assert(!isAllowed(snap, 999));

  // Whitespace tolerated everywhere.
  Policy w = parseAllowedVersions("  1.20 ,  767 - 768 ,  340 ");
  assert(w.warnings.empty());
  assert(isAllowed(w, 763));
  assert(isAllowed(w, 767) && isAllowed(w, 768));
  assert(isAllowed(w, 340));

  // Reversed range is an error warning and applies nothing.
  Policy rev = parseAllowedVersions("762-759");
  assert(rev.warnings.size() == 1);
  assert(rev.allowed.empty());
  assert(!isAllowed(rev, 759));

  // Out-of-range PVN warns; malformed item warns.
  Policy bad = parseAllowedVersions("99999, 767, 1.2.3.4");
  assert(bad.warnings.size() == 2);
  assert(isAllowed(bad, 767));

  // summarize produces a non-empty description.
  assert(!summarize(p).empty());

  std::printf("test_version_policy: OK\n");
  return 0;
}
