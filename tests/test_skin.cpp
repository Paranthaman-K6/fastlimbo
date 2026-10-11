// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "server/skin.h"
#include "protocol/buffer.h"
#include "security/velocity.h"

using namespace limbo;

// ---------------------------------------------------------------------------
// Helper: build a minimal valid Velocity MODERN payload (HMAC || body) with
// a textures property, and verify it with a known secret.  Returns the
// textures struct on success, or aborts the test on failure.
// ---------------------------------------------------------------------------
static limbo::server::skin::Textures makeTexturesPayload(
    const std::string& secret,
    const std::string& textureValue = "some-base64-texture-value",
    const std::string& textureSig = "some-base64-signature")
{
    // Build the body without HMAC first: version(1) + address + UUID + username + props
    limbo::proto::Writer body;
    body.varInt(3);                // version
    body.str("127.0.0.1");         // address
    // UUID: 16 bytes of zeros
    std::array<uint8_t,16> uuid{}; body.uuid(uuid);
    body.str("TestPlayer");        // username
    body.varInt(1);                // 1 property
    // textures property: name, value, hasSig=true, signature
    body.str("textures");          // name
    body.str(textureValue);        // value
    body.boolean(true);            // hasSig
    body.str(textureSig);          // signature

    std::vector<uint8_t> bodyBytes = body.b;

    // Prepend HMAC-SHA256
    auto sig = limbo::security::hmacSha256(secret, bodyBytes.data(), bodyBytes.size());
    std::vector<uint8_t> payload = sig;
    payload.insert(payload.end(), bodyBytes.begin(), bodyBytes.end());

    // Verify via C++ verifyModern to make sure payload is well-formed
    limbo::security::ForwardedProfile fp; std::string err;
    bool ok = limbo::security::verifyModern(payload, secret, fp, err);
    assert(ok && "Velocity payload verify should succeed");

    // Return a Textures struct with the expected values
    limbo::server::skin::Textures tex;
    tex.value = textureValue;
    tex.signature = textureSig;
    return tex;
}

// ----------------------------------------------------------------------------
// Test 1: textures passthrough (value + signature preserved byte-exact)
// ----------------------------------------------------------------------------
int main() {
  using namespace limbo;
  const std::string secret = "s3cret-velocity";

  // --- Test 1: textures passthrough (value+signature preserved byte-exact) ---
  printf("Test 1: textures passthrough... ");
  fflush(stdout);

  auto tex = makeTexturesPayload(secret, "val1", "sig1");

  // Build a LoginSuccess body with textures for PVN 767 (1.21.1)
  std::array<uint8_t, 16> uuid{}; uuid[0] = 0x01; uuid[1] = 0x02; uuid[2] = 0x03; uuid[3] = 0x04;
  auto lsBody = limbo::server::skin::buildLoginSuccessBody(767, uuid, "TestPlayer", tex);

  // Parse the properties back from the LoginSuccess body
  limbo::proto::Reader r(lsBody.data(), lsBody.size());
  assert(r.ok && "LoginSuccess body should parse ok");

  // Skip UUID (binary for pvn >= 735)
  r.pos += 16; // UUID 16 bytes

  // Skip username
  std::string username = r.str(16);
  assert(r.ok && "username should parse");
  (void)username;

  // Read properties count
  int32_t propCount = r.varInt();
  assert(r.ok && "properties count should parse");
  assert(propCount == 1 && "should have exactly 1 property (textures)");

  // Read the property: name, value, hasSig, signature
  std::string propName = r.str(255);
  assert(r.ok && "prop name should parse");
  assert(propName == "textures" && "property should be 'textures'");

  std::string propValue = r.str(32767);
  assert(r.ok && "prop value should parse");
  assert(propValue == "val1" && "texture value should be preserved byte-exact");

  bool hasSig = r.boolean();
  assert(r.ok && "hasSig should parse");
  assert(hasSig == true && "should have signature");

  std::string propSig = r.str(1024);
  assert(r.ok && "sig should parse");
  assert(propSig == "sig1" && "texture signature should be preserved byte-exact");

  // PVN 767 strict-error-handling flag (boolean false) trails the properties.
  bool strict = r.boolean();
  assert(r.ok && "strict flag should parse");
  assert(strict == false && "767 strict flag must be false");

  // Consume remaining (should be 0; 776+ would carry a 16B session UUID)
  assert(r.remaining() == 0 && "should consume all bytes");

  printf("PASS\n");

  // ----------------------------------------------------------------------------
  // Test 2: missing textures → empty properties (no crash)
  // ----------------------------------------------------------------------------
  printf("Test 2: missing textures → empty properties... ");
  fflush(stdout);

  // Build LoginSuccess without textures for PVN 767
  lsBody = limbo::server::skin::buildLoginSuccessBody(767, uuid, "TestPlayer", std::nullopt);

  limbo::proto::Reader r2(lsBody.data(), lsBody.size());
  assert(r2.ok && "LoginSuccess body without textures should parse ok");

  r2.pos += 16; // UUID
  std::string un = r2.str(16);
  assert(r2.ok && "username should parse");

  int32_t propCount2 = r2.varInt();
  assert(r2.ok && "properties count should parse");
  assert(propCount2 == 0 && "should have 0 properties when textures absent");
  // PVN 767 strict flag still present even with zero properties.
  bool strict2 = r2.boolean();
  assert(r2.ok && "strict flag should parse");
  assert(strict2 == false);
  assert(r2.remaining() == 0);

  printf("PASS\n");

  // ----------------------------------------------------------------------------
  // Test 3: era gating — legacy (PVN 47) vs modern (PVN 767) shapes
  // ----------------------------------------------------------------------------
  printf("Test 3: era gating legacy vs modern... ");
  fflush(stdout);

  // Legacy PVN 47: buildLoginSuccessBody should omit properties slot entirely
  lsBody = limbo::server::skin::buildLoginSuccessBody(47, uuid, "TestPlayer", std::nullopt);
  assert(lsBody.size() > 0 && "legacy body should be non-empty");

  // For legacy, UUID is sent as dashed string, no properties varInt after username
  limbo::proto::Reader rLegacy(lsBody.data(), lsBody.size());
  assert(rLegacy.ok && "legacy body should parse ok");

  // Pre-1.16 reads UUID as dashed string via str(), not binary
  std::string dashedUuid = rLegacy.str(32767);
  assert(rLegacy.ok && "dashed UUID should parse");
  assert(dashedUuid.size() > 0 && "dashed UUID should be non-empty");

  // After the UUID string comes the username string
  std::string legacyUsername = rLegacy.str(16);
  assert(rLegacy.ok && "legacy username should parse");
  assert(legacyUsername == "TestPlayer" && "username should match");

  // Modern PVN 767 with textures: should have properties section
  lsBody = limbo::server::skin::buildLoginSuccessBody(767, uuid, "TestPlayer", tex);
  limbo::proto::Reader rModern(lsBody.data(), lsBody.size());
  assert(rModern.ok && "modern body should parse ok");

  rModern.pos += 16; // UUID binary
  std::string modUser = rModern.str(16);
  assert(rModern.ok && "modern username should parse");

  int32_t modPropCount = rModern.varInt();
  assert(rModern.ok && "modern properties count should parse");
  assert(modPropCount == 1 && "modern should have 1 property (textures)");

  printf("PASS\n");

  // ----------------------------------------------------------------------------
  // Test 4: 26.2+ session UUID trails the profile (776), absent on 775
  // ----------------------------------------------------------------------------
  printf("Test 4: session UUID gating 775 vs 776... ");
  fflush(stdout);
  auto b775 = limbo::server::skin::buildLoginSuccessBody(775, uuid, "TestPlayer", std::nullopt);
  limbo::proto::Reader r775(b775.data(), b775.size());
  r775.pos += 16; (void)r775.str(16); (void)r775.varInt();
  assert(r775.ok && r775.remaining() == 0 && "775 must have no trailing session UUID");
  auto b776 = limbo::server::skin::buildLoginSuccessBody(776, uuid, "TestPlayer", std::nullopt);
  limbo::proto::Reader r776(b776.data(), b776.size());
  r776.pos += 16; (void)r776.str(16); (void)r776.varInt();
  assert(r776.ok && r776.remaining() == 16 && "776 must carry a 16B session UUID");
  printf("PASS\n");

  printf("\nAll skin tests passed.\n");
  return 0;
}