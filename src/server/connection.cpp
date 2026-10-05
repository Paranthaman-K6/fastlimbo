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
#include "world/void_chunk.h"

namespace limbo::server {
namespace {

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
// playIds(pvn) at framing time. Ends on timeout, echo loss, or client close.
void enterPlay(int fd, const Config& cfg, int pvn, security::Limiter* lim) {
  auto ids = proto::playIds(pvn);
  if (ids.joinGame < 0 || ids.chunk < 0) {
    auto d = packets::playDisconnectBody(pvn, "Unsupported version");
    writeAll(fd, proto::framePacket(ids.disconnect, d));
    close(fd); return;
  }
  auto sendBody = [&](int packetId, const std::vector<uint8_t>& body) {
    return writeAll(fd, proto::framePacket(packetId, body));
  };
  static std::atomic<int> nextEntity{1};
  int entityId = nextEntity.fetch_add(1);
  world::PasteCtx pasteCtx = world::spawnPaste().value_or(world::PasteCtx{});

  if (!sendBody(ids.joinGame, play::joinGameBody(pvn, entityId))) { close(fd); return; }
  if (!sendBody(ids.abilities, play::abilitiesBody(pvn))) { close(fd); return; }
  int teleportId = 1;
  double sy = world::spawnY();
  if (!sendBody(ids.position, play::positionBody(pvn, 8.5, sy, 8.5, 0.0f, 0.0f, teleportId))) { close(fd); return; }
  if (proto::needsGameEvent13(pvn)) {
    if (!sendBody(ids.gameEvent, play::gameEventBody(pvn, 13, 0.0f))) { close(fd); return; }
  }
  auto cc = play::centerChunkBody(pvn, 0, 0);
  if (!cc.empty() && ids.centerChunk >= 0) {
    if (!sendBody(ids.centerChunk, cc)) { close(fd); return; }
  }
  auto chunk = world::buildChunkBody(pvn, 0, 0, pasteCtx);
  if (chunk.empty()) {
    auto d = packets::playDisconnectBody(pvn, "Unsupported version");
    writeAll(fd, proto::framePacket(ids.disconnect, d));
    close(fd); return;
  }
  if (ids.batchStart >= 0) {
    if (!sendBody(ids.batchStart, play::batchStartBody(pvn))) { close(fd); return; }
  }
  if (!sendBody(ids.chunk, chunk)) { close(fd); return; }
  if (ids.batchFinished >= 0) {
    if (!sendBody(ids.batchFinished, play::batchFinishedBody(pvn, 1))) { close(fd); return; }
  }

  // KeepAlive loop: 10s interval, 30s echo timeout. Short polls so movement
  // packets are drained and ignored; only KeepAlive echo refreshes the timer.
  int keepAliveSb = proto::playKeepAliveServerbound(pvn);
  int64_t kaId = (static_cast<int64_t>(rand()) << 32) | rand();
  auto kaBody = [&] { return play::keepAliveBody(pvn, kaId); };
  if (!sendBody(ids.keepAlive, kaBody())) { close(fd); return; }
  auto lastSend = std::chrono::steady_clock::now();
  auto lastEcho = lastSend;
  int32_t rid; std::vector<uint8_t> rbody;
  while (true) {
    auto now = std::chrono::steady_clock::now();
    if (now - lastSend > std::chrono::seconds(10)) {
      kaId++;
      if (!sendBody(ids.keepAlive, kaBody())) break;
      lastSend = now;
    }
    if (now - lastEcho > std::chrono::seconds(30)) break;  // timed out
    if (!readPacket(fd, 1000, cfg.maxPacketBytes, rid, rbody, lim)) {
      // poll timeout (empty read) vs real close: readPacket false on both;
      // check liveness via timers — continue on timeout, break on EOF is
      // indistinguishable here, so probe with a short re-poll:
      // if the client closed, the next KeepAlive send will fail.
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
    }
    // all other serverbound packets (movement, teleport confirm, chat): ignore.
  }
  close(fd);
}

}  // namespace

void handleConnection(int fd, std::string peerIp, const Config& cfg, int onlineCount) {
  security::LimiterConfig lcfg;
  if (cfg.maxPacketBytes > 0) lcfg.maxBytesPerCall = static_cast<std::size_t>(cfg.maxPacketBytes);
  security::Limiter lim(lcfg);
  int32_t id = 0; std::vector<uint8_t> body;
  // 1. Handshake (handshake state, id 0x00).
  if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim)) { close(fd); return; }
  if (id != 0x00) { close(fd); return; }
  proto::Reader h(body);
  int32_t pvn = h.varInt();
  std::string address = h.str(255);
  uint16_t port = h.u16();
  int32_t nextState = h.varInt();
  if (!h.ok || pvn <= 0) { close(fd); return; }
  if (nextState == 3) nextState = 2;  // TRANSFER -> treat as login
  (void)address; (void)port;

  if (nextState == 1) {
    // Status: expect request 0x00, send response, expect ping 0x01, echo.
    if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x00) { close(fd); return; }
    auto resp = packets::statusResponseBody(cfg.versionName, pvn, cfg.maxPlayers, onlineCount, cfg.motd);
    if (!writeAll(fd, proto::framePacket(0x00, resp))) { close(fd); return; }
    if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x01 || body.size() != 8) { close(fd); return; }
    if (!writeAll(fd, proto::framePacket(0x01, body))) { close(fd); return; }
    close(fd); return;
  }

  if (nextState != 2) { close(fd); return; }

  // 2. Login: LoginStart 0x00.
  if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x00) { close(fd); return; }
  proto::Reader ls(body);
  std::string username = ls.str(16);
  if (!ls.ok || username.empty()) {
    auto d = packets::loginDisconnectBody(pvn, "Invalid username");
    writeAll(fd, proto::framePacket(0x00, d));
    close(fd); return;
  }
  // Ignore trailing LoginStart fields (UUID/signature/key per version) — offline/velocity only.

  if (proto::eraForPvn(pvn) == proto::Era::UNKNOWN || pvn > proto::maxSupportedPvn()) {
    auto d = packets::loginDisconnectBody(pvn, "Unsupported Minecraft version");
    writeAll(fd, proto::framePacket(0x00, d));
    close(fd); return;
  }

  std::array<uint8_t,16> uuid = proto::offlineUuid(username);
  std::string effectiveIp = peerIp;
  std::string effectiveName = username;

  // 3. Velocity modern forwarding (1.13+ only has login-plugin packets).
  if (cfg.forwarding == "MODERN") {
    if (!proto::hasLoginPlugin(pvn)) {
      auto d = packets::loginDisconnectBody(pvn, "This server requires 1.13+ via Velocity");
      writeAll(fd, proto::framePacket(0x00, d));
      close(fd); return;
    }
    auto reqData = security::pluginRequestBody();
    auto req = packets::loginPluginRequestBody(0, "velocity:player_info", reqData);
    if (!writeAll(fd, proto::framePacket(0x04, req))) { close(fd); return; }
    if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x02) {
      auto d = packets::loginDisconnectBody(pvn, "Velocity forwarding failed");
      writeAll(fd, proto::framePacket(0x00, d));
      close(fd); return;
    }
    proto::Reader pr(body);
    int32_t msgId = pr.varInt();
    bool success = pr.boolean();
    (void)msgId;
    if (!pr.ok || !success) {
      auto d = packets::loginDisconnectBody(pvn, "Velocity forwarding failed");
      writeAll(fd, proto::framePacket(0x00, d));
      close(fd); return;
    }
    std::vector<uint8_t> payload = pr.bytes(pr.remaining());
    security::ForwardedProfile fp; std::string err;
    if (!security::verifyModern(payload, cfg.forwardingSecret, fp, err)) {
      auto d = packets::loginDisconnectBody(pvn, "Unable to verify player details");
      writeAll(fd, proto::framePacket(0x00, d));
      close(fd); return;
    }
    uuid = fp.uuid; effectiveName = fp.username; effectiveIp = fp.address;
  }

  printf("[login] user=%s uuid=%s ip=%s pvn=%d (%s)\n", effectiveName.c_str(),
         uuidHex(uuid).c_str(), effectiveIp.c_str(), pvn, proto::versionName(pvn));
  fflush(stdout);

  // 4. LoginSuccess.
  auto success = packets::loginSuccessBody(pvn, uuid, effectiveName);
  if (!writeAll(fd, proto::framePacket(0x02, success))) { close(fd); return; }

  // 5. Play: pre-1.20.2 enters directly; 1.20.2+ via Configuration registry.
  const std::string msg = "Connected - void world coming soon (v0.1 login ok)";
  (void)msg;
  if (!proto::hasConfiguration(pvn)) {
    enterPlay(fd, cfg, pvn, &lim);
    return;
  }
  // Configuration path (1.20.2+): LoginAck -> [Settings] -> RegistryData ->
  // Finish -> Finish ack -> Play burst (void + keepalive).
  if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body, &lim) || id != 0x03) { close(fd); return; }
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
    }
    for (auto& blob : registry::buildRegistryBlobs(pvn)) {
      if (!writeAll(fd, proto::framePacket(ids.registryData, blob))) { close(fd); return; }
    }
    if (!writeAll(fd, proto::framePacket(ids.finish, registry::buildFinish(pvn)))) { close(fd); return; }
    // Await the Finish ack; tolerate stragglers (late Settings etc.).
    // Bounded by readTimeoutMs; HUP/ERR closes fast, idle slices continue.
    int ackId = registry::configFinishAckId(pvn);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.readTimeoutMs);
    bool acked = false;
    int ignored = 0;
    while (std::chrono::steady_clock::now() < deadline && ignored < 16) {
      pollfd pp{fd, POLLIN, 0};
      int pr = poll(&pp, 1, 1000);
      if (pr < 0) break;
      if (pr == 0) continue;
      if (pp.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
      if (!(pp.revents & POLLIN)) break;
      if (!readPacket(fd, 5000, cfg.maxPacketBytes, id, body, &lim)) break;
      if (id == ackId) { acked = true; break; }
      ++ignored;
    }
    if (!acked) { close(fd); return; }
  }
  enterPlay(fd, cfg, pvn, &lim);
  return;
}

}  // namespace limbo::server
