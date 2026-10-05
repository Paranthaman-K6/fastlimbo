#include "security/velocity.h"
#include <openssl/hmac.h>
#include "protocol/buffer.h"

namespace limbo::security {

std::vector<uint8_t> hmacSha256(const std::string& key, const uint8_t* msg, size_t len) {
  std::vector<uint8_t> out(32);
  unsigned int outLen = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), msg, len, out.data(), &outLen);
  out.resize(outLen);
  return out;
}

bool constantTimeEq(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= (a[i] ^ b[i]);
  // Prevent optimization into early-exit: use volatile sink.
  volatile unsigned char sink = diff;
  (void)sink;
  return diff == 0;
}

std::vector<uint8_t> pluginRequestBody() {
  proto::Writer w;
  w.varInt(1);  // max supported forwarding version we request
  return w.b;
}

bool verifyModern(const std::vector<uint8_t>& payload, const std::string& secret,
                  ForwardedProfile& out, std::string& err) {
  if (payload.size() < 32 + 1) { err = "forwarding payload too short"; return false; }
  std::vector<uint8_t> sig(payload.begin(), payload.begin() + 32);
  const uint8_t* body = payload.data() + 32;
  size_t bodyLen = payload.size() - 32;
  auto expect = hmacSha256(secret, body, bodyLen);
  if (!constantTimeEq(sig, expect)) { err = "bad forwarding signature"; return false; }

  proto::Reader r(body, bodyLen);
  int32_t ver = r.varInt();
  if (!r.ok || ver < 1 || ver > 4) { err = "unsupported forwarding version"; return false; }
  out.address = r.str(255);
  // Velocity strips IPv6 '%' zone scopes ("fe80::1%eth0" -> "fe80::1").
  if (auto pct = out.address.find('%'); pct != std::string::npos)
    out.address.resize(pct);
  out.uuid = r.uuid();
  out.username = r.str(16);
  int32_t props = r.varInt();
  if (!r.ok || props < 0 || props > 64) { err = "bad properties count"; return false; }
  for (int i = 0; i < props; ++i) {
    std::string name = r.str(255);
    std::string value = r.str(32767);
    bool hasSig = r.boolean();
    if (hasSig) (void)r.str(1024);
    if (!r.ok) { err = "bad property"; return false; }
    (void)name; (void)value;
  }
  // v2/v3 extras (key/holder) are ignored for limbo-minimal: modern still verifies,
  // address/uuid/username already extracted. Strict length check would break v2+; accept trailing.
  if (!r.ok) { err = "bad forwarding body"; return false; }
  if (out.username.empty() || out.username.size() > 16) { err = "bad username"; return false; }
  return true;
}

}  // namespace limbo::security
