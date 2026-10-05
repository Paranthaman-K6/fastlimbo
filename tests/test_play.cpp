#include <cassert>
#include <cstdio>
#include "protocol/play.h"
#include "protocol/versions.h"

int main() {
  using namespace limbo::play;
  // Non-empty JoinGame body.
  auto jg = joinGameBody(767, 1);
  assert(!jg.empty());
  // GameEvent13 gated by needsGameEvent13(pvn).
  assert(limbo::proto::needsGameEvent13(765));
  assert(!limbo::proto::needsGameEvent13(764));
  auto ge13 = gameEventBody(765, 13, 0.0f);
  assert(!ge13.empty());
  printf("test_play ok\n");
  return 0;
}
