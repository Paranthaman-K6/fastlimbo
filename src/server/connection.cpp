// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "server/connection.h"
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include "protocol/buffer.h"
#include "protocol/packets.h"
#include "protocol/play.h"
#include "protocol/registry.h"
#include "protocol/varint.h"
#include "protocol/versions.h"
#include "security/limits.h"
#include "security/velocity.h"
#include "server/skin.h"
#include "server/version_policy.h"
#include "world/void_chunk.h"

namespace limbo::server {
namespace {

// Active version allowlist, set once at startup via setVersionPolicy().
Policy g_policy = parseAllowedVersions("all");

bool readExact(int fd, uint8_t* dst, size_t n, int timeoutMs) {
  size_t got = 0;
  while (got < n) {
    pollfd p{fd, POLLIN, 0};
    int r = poll(&p, 1, timeoutMs);
    if (r <= 0) return false;
    ssize_t k = recv(fd, dst + got, n - got, 0);
    if (k <= 0) return false;
    got += k;
  }
  return true;
}

bool writeAll(int fd, const uint8_t* p, size_t n) {
  size_t sent = 0;
  while (sent < n) {
    ssize_t k = send(fd, p + sent, n - sent, MSG_NOSIGNAL);
    if (k <= 0) return false;
    sent += k;
  }
  return true;
}
bool writeAll(int fd, const std::vector<uint8_t>& v) { return writeAll(fd, v.data(), v.size()); }

// Read one framed packet: VarInt len + VarInt id + body. Caps len at maxBytes.
// When lim != nullptr, charges len against the rate limiter (abusive -> false).
bool readPacket(int fd, int timeoutMs, int maxBytes, int32_t& id, std::vector<uint8_t>& body,
                security::Limiter* lim = nullptr) {
  // Read VarInt len byte-by-byte (max 5).
  uint32_t len = 0; int shift = 0;
  for (int i = 0; i < 5; ++i) {
    uint8_t b = 0;
    if (!readExact(fd, &b, 1, timeoutMs)) return false;
    len |= static_cast<uint32_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) break;
    shift += 7;
    if (i == 4) return false;
    if (len > static_cast<uint32_t>(maxBytes) + 5) return false;
  }
  if (len == 0 || len > static_cast<uint32_t>(maxBytes) + 5) return false;
  if (lim && !lim->take(len)) return false;  // flood guard: drop abusive connection
  std::vector<uint8_t> raw(len);
  if (!readExact(fd, raw.data(), len, timeoutMs)) return false;
  size_t pos = 0;
  auto oid = proto::readVarInt(raw, pos);
  if (!oid) return false;
  id = *oid;
  body.assign(raw.begin() + pos, raw.end());
  return true;
}

std::string uuidHex(const std::array<uint8_t,16>& u) {
  char s[33]; for (int i = 0; i < 16; ++i) snprintf(s + i * 2, 3, "%02x", u[i]);
  return std::string(s, 32);
}

// Minimal Play session: JoinGame -> Abilities -> Position -> GameEvent13? ->
// Center? -> void chunk -> KeepAlive loop. Movement/teleport-confirm/chat are
// ignored (client stays frozen at spawn). Bodies from workstreams A; IDs from
// playIds(pvn) at framing time. Returns the leave reason for [leave] logging.
// Ends on timeout, echo loss, or client close.
std::string enterPlay(int fd, const Config& cfg, int pvn, security::Limiter* lim,
                      const std::string& username, const std::string& peerIp) {
  int lvl = cfg.level();
  auto ids = proto::playIds(pvn);
  if (ids.joinGame < 0 || ids.chunk < 0) {
    auto d = packets::playDisconnectBody(pvn, "Unsupported version");
    writeAll(fd, proto::framePacket(ids.disconnect, d));
    close(fd);
    return "unsupported version (no play ids)";
  }
  auto sendBody = [&](int packetId, const std::vector<uint8_t>& body) {
    return writeAll(fd, proto::framePacket(packetId, body));
  };
  static std::atomic<int> nextEntity{1};
  int entityId = nextEntity.fetch_add(1);
  world::PasteCtx pasteCtx = world::spawnPaste().value_or(world::PasteCtx{});

  if (lvl >= 2)
    printf("[play] user=%s ip=%s pvn=%d: entering play (entity %d)\n",
           username.c_str(), peerIp.c_str(), pvn, entityId);
  if (lvl >= 2) fflush(stdout);
  if (!sendBody(ids.joinGame, play::joinGameBody(pvn, entityId))) { close(fd); return "join failed (client gone at JoinGame)"; }
  if (!sendBody(ids.abilities, play::abilitiesBody(pvn))) { close(fd); return "send failed (abilities)"; }
  int teleportId = 1;
  // Spawn point: configured X/Z, Y explicit or auto (platform top / 400 void).
  // Shared by the initial teleport and every freeze teleport-back.
  double sx = cfg.spawnX, sz = cfg.spawnZ;
  double sy = cfg.spawnAutoY ? world::spawnYAt(sx, sz) : cfg.spawnY;
  // Spawn chunk (center packet) + schematic footprint, expanded to cover
  // the spawn chunk when a custom spawn sits off the build.
  int pcx = (int)std::floor(sx / 16.0), pcz = (int)std::floor(sz / 16.0);
  int minCx = 0, maxCx = 0, minCz = 0, maxCz = 0;
  world::spawnChunkRange(minCx, maxCx, minCz, maxCz);
  if (pcx < minCx) minCx = pcx;
  if (pcx > maxCx) maxCx = pcx;
  if (pcz < minCz) minCz = pcz;
  if (pcz > maxCz) maxCz = pcz;
  // Manual chunk load: expand to the configured radius around spawn.
  // Union with the auto-detected schematic footprint (never shrinks it).
  if (pcx - cfg.chunkRadius < minCx) minCx = pcx - cfg.chunkRadius;
  if (pcx + cfg.chunkRadius > maxCx) maxCx = pcx + cfg.chunkRadius;
  if (pcz - cfg.chunkRadius < minCz) minCz = pcz - cfg.chunkRadius;
  if (pcz + cfg.chunkRadius > maxCz) maxCz = pcz + cfg.chunkRadius;
  if (!sendBody(ids.position, play::positionBody(pvn, sx, sy, sz, 0.0f, 0.0f, teleportId))) { close(fd); return "send failed (position)"; }
  if (proto::needsGameEvent13(pvn)) {
    if (!sendBody(ids.gameEvent, play::gameEventBody(pvn, 13, 0.0f))) { close(fd); return "send failed (game event)"; }
  }
  auto cc = play::centerChunkBody(pvn, pcx, pcz);
  if (!cc.empty() && ids.centerChunk >= 0) {
    if (!sendBody(ids.centerChunk, cc)) { close(fd); return "send failed (center chunk)"; }
  }
  auto chunk = world::buildChunkBody(pvn, 0, 0, pasteCtx);
  if (chunk.empty()) {
    auto d = packets::playDisconnectBody(pvn, "Unsupported version");
    writeAll(fd, proto::framePacket(ids.disconnect, d));
    close(fd);
    return "unsupported version (no chunk body)";
  }
  if (ids.batchStart >= 0) {
    if (!sendBody(ids.batchStart, play::batchStartBody(pvn))) { close(fd); return "send failed (batch start)"; }
  }
  int lightId = proto::playUpdateLightId(pvn);
  if (lightId >= 0) {
    // 1.17/1.17.1 only: standalone Update Light MUST precede the chunk.
    // Via's 1.17.1->1.18 WorldPacketRewriter1_18 cancels+caches LIGHT_UPDATE
    // for not-yet-loaded chunks and embeds the stored light into
    // LEVEL_CHUNK_WITH_LIGHT. Light-after-chunk instead logs "No light data
    // found for chunk at 0, 0" and falls back to synthesized full-bright.
    auto light = world::buildUpdateLightBody(pvn, 0, 0);
    if (!light.empty() && !sendBody(lightId, light)) { close(fd); return "send failed (update light)"; }
  }
  if (!sendBody(ids.chunk, chunk)) { close(fd); return "send failed (chunk)"; }
  // Spawn area: every chunk the schematic touches (auto-detected corner
  // blocks), plus the spawn chunk. Vanilla floods view-distance chunks on
  // login; a single chunk leaves 26.x clients stuck at "loading terrain"
  // until timeout. Out-of-schematic cells stay void.
  int sent = 1;
  for (int cx = minCx; cx <= maxCx; ++cx)
    for (int cz = minCz; cz <= maxCz; ++cz) {
      if (cx == 0 && cz == 0) continue;
      auto c = world::buildChunkBody(pvn, cx, cz, pasteCtx);
      if (c.empty()) continue;
      if (!sendBody(ids.chunk, c)) { close(fd); return "send failed (chunk)"; }
      sent++;
    }
  if (ids.batchFinished >= 0) {
    if (!sendBody(ids.batchFinished, play::batchFinishedBody(pvn, sent))) { close(fd); return "send failed (batch finished)"; }
  }

  // KeepAlive loop: 10s interval, 30s echo timeout. Short polls so movement
  // packets are drained and answered (spawn freeze) and only KeepAlive echo
  // refreshes the timer.
  int keepAliveSb = proto::playKeepAliveServerbound(pvn);
  proto::MoveIds mv = proto::playMovementServerbound(pvn);
  int64_t kaId = (static_cast<int64_t>(rand()) << 32) | rand();
  auto kaBody = [&] { return play::keepAliveBody(pvn, kaId); };
  if (!sendBody(ids.keepAlive, kaBody())) { close(fd); return "send failed (keepalive)"; }
  auto lastSend = std::chrono::steady_clock::now();
  auto lastEcho = lastSend;
  int32_t rid; std::vector<uint8_t> rbody;
  std::string playReason = "client closed connection";
  while (true) {
    auto now = std::chrono::steady_clock::now();
    if (now - lastSend > std::chrono::seconds(10)) {
      kaId++;
      if (!sendBody(ids.keepAlive, kaBody())) { playReason = "send failed (keepalive refresh — client gone)"; break; }
      lastSend = now;
    }
    if (now - lastEcho > std::chrono::seconds(30)) {
      playReason = "keepalive timeout (no echo for 30s)";
      break;  // timed out
    }
    if (!readPacket(fd, 1000, cfg.maxPacketBytes, rid, rbody, lim)) {
      // Distinguish idle timeout (client still there) from a dead peer:
      // a closed socket shows HUP/ERR on poll; an idle one stays quiet.
      pollfd pp{fd, POLLIN, 0};
      int pr = poll(&pp, 1, 0);
      if (pr > 0 && (pp.revents & (POLLHUP | POLLERR | POLLNVAL))) {
        playReason = "client closed connection";
        break;
      }
      // Also probe: a RST peer fails the next send; keep looping on plain
      // idle timeouts so movement drains and keepalive refreshes work.
      continue;
    }
    if (rid == keepAliveSb) {
      proto::Reader r(rbody);
      bool echoOk = false;
      if (pvn == 47) {
        echoOk = r.ok && (r.varInt() == static_cast<int32_t>(kaId & 0x7FFFFFFF));
      } else if (rbody.size() == 8) {
        echoOk = r.ok && r.i64() == kaId;
      }
      if (echoOk) lastEcho = std::chrono::steady_clock::now();
    } else if (rid == mv.confirm && mv.confirm >= 0 && cfg.freezePlayer) {
      // Teleport confirm: accept silently, never reply (replying would loop:
      // teleport -> confirm -> teleport ...). Trace-log unexpected ids.
      if (lvl >= 3) {
        proto::Reader r(rbody);
        int32_t acked = r.ok ? r.varInt() : -1;
        if (acked != teleportId)
          printf("[trace] play user=%s pvn=%d: stale teleport confirm %d (outstanding %d)\n",
                 username.c_str(), pvn, acked, teleportId);
        fflush(stdout);
      }
    } else if ((rid == mv.pos || rid == mv.posRot || rid == mv.vehicle) && cfg.freezePlayer) {
      // Spawn freeze: any position-carrying packet snaps the player back to
      // spawn with a fresh teleport id. Bodies are never parsed (fixed
      // spawn); the frame was already drained by readPacket.
      teleportId++;
      if (lvl >= 2) {
        printf("[debug] play user=%s pvn=%d: movement id=0x%x, teleport-back %d\n",
               username.c_str(), pvn, rid, teleportId);
        fflush(stdout);
      }
      if (!sendBody(ids.position, play::positionBody(pvn, sx, sy, sz, 0.0f, 0.0f, teleportId))) {
        playReason = "send failed (teleport-back — client gone)";
        break;
      }
    }
    // all other serverbound packets (look-only, steer/input, chat): ignore.
    if (lvl >= 3 && rid != keepAliveSb && rid != mv.confirm &&
        rid != mv.pos && rid != mv.posRot && rid != mv.vehicle)
      printf("[trace] play user=%s pvn=%d: serverbound id=0x%x (%zuB) ignored\n",
             username.c_str(), pvn, rid, rbody.size());
  }
  if (lvl >= 3) fflush(stdout);
  close(fd);
  return playReason;
}

}  // namespace

void setVersionPolicy(const std::string& spec) { g_policy = parseAllowedVersions(spec); }

const Policy& versionPolicy() { return g_policy; }

void handleConnection(int fd, std::string peerIp, const Config& cfg,
                      std::atomic<int>& online) {
  int lvl = cfg.level();  // 0=ERROR 1=INFO 2=DEBUG 3=TRACE
  auto t0 = std::chrono::steady_clock::now();
  auto secs = [&] {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - t0)
        .count();
  };
  security::LimiterConfig lcfg;
  if (cfg.maxPacketBytes > 0) lcfg.maxBytesPerCall = static_cast<std::size_t>(cfg.maxPacketBytes);
  security::Limiter lim(lcfg);
  int32_t id = 0; std::vector<uint8_t> body;
  // Player slot guard: incremented once LoginSuccess is flushed, released on
  // every leave path after that. Status pings never touch the counter.
  struct PlayerSlot {
    std::atomic<int>& n; bool held = false;
    void hold() { if (!held) { n++; held = true; } }
    ~PlayerSlot() { if (held) n--; }
  } slot{online};
  std::string who = "?"; int whatPvn = 0;
  auto logLeave = [&](const std::string& stage, const std::string& reason) {
    // Show the count AFTER this player leaves (slot still held at call time).
    int after = online.load() - (slot.held ? 1 : 0);
    if (lvl >= 1)
      printf("[leave] user=%s ip=%s pvn=%d stage=%s after=%llds online=%d reason: %s\n",
             who.c_str(), peerIp.c_str(), whatPvn, stage.c_str(),
             (long long)secs(), after, reason.c_str());
    if (lvl >= 1) fflush(stdout);
  };
  // 1. Handshake (handshake state, id 0x00).
  if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim)) {
    if (lvl >= 2) { printf("[debug] connect ip=%s: handshake timeout/close\n", peerIp.c_str()); fflush(stdout); }
    close(fd); return;
  }
  if (id != 0x00) { close(fd); return; }
  proto::Reader h(body);
  int32_t pvn = h.varInt();
  std::string address = h.str(255);
  uint16_t port = h.u16();
  int32_t nextState = h.varInt();
  if (!h.ok || pvn <= 0) { close(fd); return; }
  if (nextState == 3) nextState = 2;  // TRANSFER -> treat as login
  if (lvl >= 2) {
    printf("[debug] handshake ip=%s pvn=%d next=%d addr=%s\n", peerIp.c_str(), pvn,
           nextState, address.c_str());
    fflush(stdout);
  }
  (void)address; (void)port;

  if (nextState == 1) {
    // Status: expect request 0x00, send response, expect ping 0x01, echo.
    // Never counts toward online players; count is read live at response time.
    if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x00) { close(fd); return; }
    auto resp = packets::statusResponseBody(cfg.versionName, pvn, cfg.maxPlayers, online.load(), cfg.motd);
    if (!writeAll(fd, proto::framePacket(0x00, resp))) { close(fd); return; }
    if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x01 || body.size() != 8) { close(fd); return; }
    if (!writeAll(fd, proto::framePacket(0x01, body))) { close(fd); return; }
    if (lvl >= 3) { printf("[trace] status ip=%s pvn=%d online=%d ok\n", peerIp.c_str(), pvn, online.load()); fflush(stdout); }
    close(fd); return;
  }

  if (nextState != 2) { close(fd); return; }

  // 2. Login: LoginStart 0x00.
  if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x00) {
    if (lvl >= 1) printf("[leave] ip=%s pvn=%d stage=login after=%llds reason: no LoginStart (closed/timeout)\n",
                         peerIp.c_str(), pvn, (long long)secs());
    if (lvl >= 1) fflush(stdout);
    close(fd); return;
  }
  proto::Reader ls(body);
  std::string username = ls.str(16);
  if (!ls.ok || username.empty()) {
    auto d = packets::loginDisconnectBody(pvn, "Invalid username");
    writeAll(fd, proto::framePacket(0x00, d));
    close(fd); return;
  }
  // Ignore trailing LoginStart fields (UUID/signature/key per version) — offline/velocity only.

  who = username; whatPvn = pvn;
  if (online.load() >= cfg.maxPlayers) {
    auto d = packets::loginDisconnectBody(pvn, "Server is full");
    writeAll(fd, proto::framePacket(0x00, d));
    printf("[reject] user=%s ip=%s pvn=%d: server full (%d/%d)\n",
           username.c_str(), peerIp.c_str(), pvn, online.load(), cfg.maxPlayers);
    fflush(stdout);
    logLeave("login", "server full");
    close(fd); return;
  }
  if (proto::eraForPvn(pvn) == proto::Era::UNKNOWN || pvn > proto::maxSupportedPvn() ||
      !isAllowed(g_policy, pvn)) {
    auto d = packets::loginDisconnectBody(pvn, "Unsupported Minecraft version");
    writeAll(fd, proto::framePacket(0x00, d));
    printf("[reject] user=%s ip=%s pvn=%d: unsupported version\n",
           username.c_str(), peerIp.c_str(), pvn);
    fflush(stdout);
    logLeave("login", "unsupported version");
    close(fd); return;
  }

  std::array<uint8_t,16> uuid = proto::offlineUuid(username);
  std::string effectiveIp = peerIp;
  std::string effectiveName = username;
  std::optional<skin::Textures> textures;  // preserved for LoginSuccess (1.19+)

  // 3. Velocity modern forwarding (1.13+ only has login-plugin packets).
  if (cfg.forwarding == "MODERN") {
    if (!proto::hasLoginPlugin(pvn)) {
      auto d = packets::loginDisconnectBody(pvn, "This server requires 1.13+ via Velocity");
      writeAll(fd, proto::framePacket(0x00, d));
      logLeave("login", "velocity requires 1.13+");
      close(fd); return;
    }
    auto reqData = security::pluginRequestBody();
    auto req = packets::loginPluginRequestBody(0, "velocity:player_info", reqData);
    if (!writeAll(fd, proto::framePacket(0x04, req))) { logLeave("login", "send failed (velocity request)"); close(fd); return; }
    if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x02) {
      auto d = packets::loginDisconnectBody(pvn, "Velocity forwarding failed");
      writeAll(fd, proto::framePacket(0x00, d));
      logLeave("login", "velocity forwarding failed (no plugin response 0x02)");
      close(fd); return;
    }
    proto::Reader pr(body);
    int32_t msgId = pr.varInt();
    bool success = pr.boolean();
    (void)msgId;
    if (!pr.ok || !success) {
      auto d = packets::loginDisconnectBody(pvn, "Velocity forwarding failed");
      writeAll(fd, proto::framePacket(0x00, d));
      logLeave("login", "velocity forwarding failed (negative response)");
      close(fd); return;
    }
    std::vector<uint8_t> payload = pr.bytes(pr.remaining());
    security::ForwardedProfile fp; std::string err;
    if (!security::verifyModern(payload, cfg.forwardingSecret, fp, err)) {
      auto d = packets::loginDisconnectBody(pvn, "Unable to verify player details");
      writeAll(fd, proto::framePacket(0x00, d));
      printf("[reject] user=%s ip=%s pvn=%d: velocity verify failed (%s)\n",
             username.c_str(), peerIp.c_str(), pvn, err.c_str());
      fflush(stdout);
      logLeave("login", std::string("velocity verify failed: ") + err);
      close(fd); return;
    }
    uuid = fp.uuid; effectiveName = fp.username; effectiveIp = fp.address;
    who = effectiveName;
    // Preserve Velocity textures property for LoginSuccess skins (offline-safe,
    // no Mojang fetch; absent -> default Steve/Alex).
    textures = skin::parseTexturesFromVelocityPayload(payload);
  }

  printf("[login] user=%s uuid=%s ip=%s pvn=%d (%s) skin=%s\n", effectiveName.c_str(),
         uuidHex(uuid).c_str(), effectiveIp.c_str(), pvn, proto::versionName(pvn),
         textures ? "textures" : "default");
  fflush(stdout);

  // 4. LoginSuccess (1.19+ carries textures property when present;
  // 26.2+ carries a trailing session UUID).
  auto success = skin::buildLoginSuccessBody(pvn, uuid, effectiveName, textures);
  if (!writeAll(fd, proto::framePacket(0x02, success))) { logLeave("login", "send failed (LoginSuccess)"); close(fd); return; }
  slot.hold();  // real player from here on; status pings never reach this line
  if (lvl >= 2) { printf("[debug] user=%s pvn=%d: LoginSuccess sent (%zuB) online=%d\n", effectiveName.c_str(), pvn, success.size(), online.load()); fflush(stdout); }

  // 5. Play: pre-1.20.2 enters directly; 1.20.2+ via Configuration registry.
  if (!proto::hasConfiguration(pvn)) {
    std::string reason = enterPlay(fd, cfg, pvn, &lim, effectiveName, effectiveIp);
    logLeave("play", reason);
    return;
  }
  // Configuration path (1.20.2+): LoginAck -> [Settings] -> RegistryData ->
  // Tags? -> KnownPacks? -> Finish -> Finish ack -> Play burst (void+keepalive).
  if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x03) {
    logLeave("config", "no LoginAcknowledged 0x03 (client closed, timed out, or wrong id)");
    close(fd); return;
  }
  if (lvl >= 2) { printf("[debug] user=%s pvn=%d: LoginAck received\n", effectiveName.c_str(), pvn); fflush(stdout); }
  {
    auto ids = registry::configIds(pvn);
    // Vanilla clients send Client Information (Settings, 0x00) immediately on
    // entering Configuration — possibly before our burst. Drain anything that
    // already arrived (200ms grace), then send. poll() tells a dead peer
    // (HUP/ERR) from an idle one, so this never spins.
    for (int i = 0; i < 4; ++i) {
      pollfd pp{fd, POLLIN, 0};
      if (poll(&pp, 1, 200) <= 0) break;
      if (!(pp.revents & POLLIN)) break;
      int32_t did;
      std::vector<uint8_t> dbody;
      if (!readPacket(fd, 2000, cfg.maxPacketBytes, did, dbody, &lim)) break;
      if (lvl >= 3) printf("[trace] config user=%s: early serverbound id=0x%x (%zuB) drained\n", effectiveName.c_str(), did, dbody.size());
    }
    auto blobs = registry::buildRegistryBlobs(pvn);
    size_t regBytes = 0;
    for (auto& blob : blobs) {
      regBytes += blob.size();
      if (!writeAll(fd, proto::framePacket(ids.registryData, blob))) { logLeave("config", "send failed (RegistryData)"); close(fd); return; }
    }
    if (lvl >= 2) { printf("[debug] user=%s pvn=%d: sent %zu RegistryData (0x%02x, %zuB)\n", effectiveName.c_str(), pvn, blobs.size(), ids.registryData, regBytes); fflush(stdout); }
    int tagsId = registry::configTagsId(pvn);
    if (tagsId > 0) {
      auto tags = registry::buildTags(pvn);
      if (!writeAll(fd, proto::framePacket(tagsId, tags))) { logLeave("config", "send failed (Update Tags)"); close(fd); return; }
      if (lvl >= 2) { printf("[debug] user=%s pvn=%d: sent Update Tags 0x%02x\n", effectiveName.c_str(), pvn, tagsId); fflush(stdout); }
    }
    int packsId = registry::configKnownPacksId(pvn);
    if (packsId > 0) {
      auto packs = registry::buildKnownPacks(pvn);
      if (!writeAll(fd, proto::framePacket(packsId, packs))) { logLeave("config", "send failed (Known Packs)"); close(fd); return; }
      if (lvl >= 2) { printf("[debug] user=%s pvn=%d: sent Known Packs 0x%02x\n", effectiveName.c_str(), pvn, packsId); fflush(stdout); }
    }
    if (!writeAll(fd, proto::framePacket(ids.finish, registry::buildFinish(pvn)))) { logLeave("config", "send failed (Finish)"); close(fd); return; }
    if (lvl >= 2) { printf("[debug] user=%s pvn=%d: sent Finish 0x%02x, awaiting ack 0x%02x\n", effectiveName.c_str(), pvn, ids.finish, registry::configFinishAckId(pvn)); fflush(stdout); }
    // Await the Finish ack; tolerate stragglers (late Settings etc.).
    // Bounded by readTimeoutMs; HUP/ERR closes fast, idle slices continue.
    int ackId = registry::configFinishAckId(pvn);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.readTimeoutMs);
    bool acked = false;
    int ignored = 0;
    int lastIgnored = -1;
    int knownPacksSb = registry::configKnownPacksServerboundId(pvn);
    while (std::chrono::steady_clock::now() < deadline && ignored < 32) {
      pollfd pp{fd, POLLIN, 0};
      int pr = poll(&pp, 1, 1000);
      if (pr < 0) break;
      if (pr == 0) continue;
      if (pp.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
      if (!(pp.revents & POLLIN)) break;
      if (!readPacket(fd, 5000, cfg.maxPacketBytes, id, body, &lim)) break;
      if (id == ackId) { acked = true; break; }
      ++ignored; lastIgnored = id;
      // Known Packs answer (serverbound 0x07) and Client Information (0x00)
      // are normal here — tolerated, never an error.
      if (lvl >= 3)
        printf("[trace] config user=%s: serverbound id=0x%x (%zuB) while awaiting ack%s\n",
               effectiveName.c_str(), id, body.size(),
               id == knownPacksSb ? " (Known Packs answer)" : id == 0x00 ? " (Client Information)" : "");
    }
    if (lvl >= 3) fflush(stdout);
    if (!acked) {
      char why[160];
      snprintf(why, sizeof(why),
               "no finish-ack (waited %ds, ignored %d packet(s), last=0x%x) — client likely rejected Registry/Tags/Packs or closed",
               cfg.readTimeoutMs / 1000, ignored, lastIgnored);
      printf("[config] user=%s ip=%s pvn=%d: %s\n",
             effectiveName.c_str(), effectiveIp.c_str(), pvn, why);
      fflush(stdout);
      logLeave("config", why);
      close(fd); return;
    }
    if (lvl >= 2) { printf("[debug] user=%s pvn=%d: Finish ack received (ignored %d)\n", effectiveName.c_str(), pvn, ignored); fflush(stdout); }
  }
  std::string reason = enterPlay(fd, cfg, pvn, &lim, effectiveName, effectiveIp);
  logLeave("play", reason);
  return;
}

}  // namespace limbo::server
