// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include <cassert>
#include <cstdio>
#include "security/velocity.h"
#include "protocol/buffer.h"

int main() {
  using namespace limbo;
  // Build a v1 payload, sign it, verify ok; tamper fails.
  proto::Writer body;
  body.varInt(1);
  body.str("127.0.0.1");
  std::array<uint8_t,16> uuid{}; for (int i = 0; i < 16; ++i) uuid[i] = i;
  body.uuid(uuid);
  body.str("TestPlayer");
  body.varInt(0);
  std::string secret = "s3cret-velocity";
  auto sig = security::hmacSha256(secret, body.b.data(), body.b.size());
  std::vector<uint8_t> payload = sig;
  payload.insert(payload.end(), body.b.begin(), body.b.end());

  security::ForwardedProfile fp; std::string err;
  assert(security::verifyModern(payload, secret, fp, err));
  assert(fp.username == "TestPlayer" && fp.address == "127.0.0.1" && fp.uuid == uuid);

  payload[payload.size()-1] ^= 1;
  assert(!security::verifyModern(payload, secret, fp, err));
  assert(!security::constantTimeEq(std::vector<uint8_t>{1,2}, std::vector<uint8_t>{1}));

  // IPv6 with zone scope must verify and strip at '%'.
  proto::Writer v6;
  v6.varInt(1);
  v6.str("fe80::1%eth0");
  v6.uuid(uuid);
  v6.str("V6Player");
  v6.varInt(0);
  auto sig6 = security::hmacSha256(secret, v6.b.data(), v6.b.size());
  std::vector<uint8_t> p6 = sig6;
  p6.insert(p6.end(), v6.b.begin(), v6.b.end());
  assert(security::verifyModern(p6, secret, fp, err));
  assert(fp.address == "fe80::1" && fp.username == "V6Player");
  printf("test_velocity ok\n");
  return 0;
}
