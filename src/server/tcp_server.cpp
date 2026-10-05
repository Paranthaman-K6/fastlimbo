// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "server/tcp_server.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include "server/connection.h"

namespace limbo::server {
namespace {

// Resolve bind host to in6_addr for a dual-stack (v6_only=0) listener.
// Supports "0.0.0.0"/"::"/"" (any), numeric v6/v4, and hostnames via getaddrinfo.
// Context7 Boost.Asio docs confirm dual-stack semantics: a single IPv6 socket with
// IPV6_V6ONLY=0 accepts both families; v4 appears as ::ffff:a.b.c.d.
bool resolveBind(const std::string& host, in6_addr& out, std::string& err) {
  if (host.empty() || host == "0.0.0.0" || host == "::" || host == "*") {
    out = in6addr_any;
    return true;
  }
  if (inet_pton(AF_INET6, host.c_str(), &out) == 1) return true;
  in_addr v4{};
  if (inet_pton(AF_INET, host.c_str(), &v4) == 1) {
    memset(&out, 0, sizeof(out));
    out.s6_addr[10] = 0xFF; out.s6_addr[11] = 0xFF;
    memcpy(&out.s6_addr[12], &v4, 4);
    return true;
  }
  addrinfo hints{};
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0) {
    out = ((sockaddr_in6*)res->ai_addr)->sin6_addr;
    freeaddrinfo(res);
    return true;
  }
  hints.ai_family = AF_INET;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0) {
    in_addr h4 = ((sockaddr_in*)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    memset(&out, 0, sizeof(out));
    out.s6_addr[10] = 0xFF; out.s6_addr[11] = 0xFF;
    memcpy(&out.s6_addr[12], &h4, 4);
    return true;
  }
  err = "cannot resolve bind host " + host;
  return false;
}

// Render peer address: v4-mapped as dotted-quad, v6 as canonical text.
std::string peerIp(const sockaddr_storage& peer) {
  char txt[INET6_ADDRSTRLEN] = "?";
  if (peer.ss_family == AF_INET6) {
    auto* a = (const sockaddr_in6*)&peer;
    if (IN6_IS_ADDR_V4MAPPED(&a->sin6_addr)) {
      in_addr v4; memcpy(&v4, &a->sin6_addr.s6_addr[12], 4);
      if (inet_ntop(AF_INET, &v4, txt, sizeof(txt))) return txt;
    } else if (inet_ntop(AF_INET6, &a->sin6_addr, txt, sizeof(txt))) {
      return txt;
    }
  } else if (peer.ss_family == AF_INET) {
    auto* a = (const sockaddr_in*)&peer;
    if (inet_ntop(AF_INET, &a->sin_addr, txt, sizeof(txt))) return txt;
  }
  return txt;
}

}  // namespace

int run(const Config& cfg) {
  int srv = socket(AF_INET6, SOCK_STREAM, 0);
  if (srv < 0) { perror("socket"); return 1; }
  int off = 0;
  setsockopt(srv, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in6 addr{};
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(cfg.port);
  std::string resolveErr;
  if (!resolveBind(cfg.bind, addr.sin6_addr, resolveErr)) {
    fprintf(stderr, "%s\n", resolveErr.c_str());
    close(srv);
    return 1;
  }
  if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
  if (listen(srv, 511) < 0) { perror("listen"); return 1; }
  printf("limbo listening on [%s]:%d forwarding=%s\n", cfg.bind.c_str(), cfg.port, cfg.forwarding.c_str());
  fflush(stdout);

  std::atomic<int> online{0};
  while (true) {
    sockaddr_storage peer{}; socklen_t plen = sizeof(peer);
    int fd = accept(srv, (sockaddr*)&peer, &plen);
    if (fd < 0) continue;
    if (online.load() >= cfg.maxPlayers) { close(fd); continue; }
    std::string ip = peerIp(peer);
    online++;
    std::thread([fd, ip, &cfg, &online]() {
      handleConnection(fd, ip, cfg, online.load());
      online--;
    }).detach();
  }
  return 0;
}

}  // namespace limbo::server
