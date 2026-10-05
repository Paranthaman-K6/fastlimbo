#!/usr/bin/env python3
"""Soak harness for limbo: join latency + concurrent idle holders, with server RSS.

Two phases against a live server (the lead starts the server separately):

  Phase 1 "latency"  -- N sequential offline logins (PVN 763, 1.20.1). Measures
                        time to the first Play packet (JoinGame), plus connect
                        and LoginSuccess times, plus the Play burst shape.
  Phase 2 "holders"  -- C concurrent idle holders: full login -> Play burst, then
                        echo clientbound KeepAlives for H seconds so the server's
                        30s echo deadline is never hit, then close.

Server RSS is sampled through `ps` before / during (peak) / after.

Usage:
    python3 tests/soak/soak.py [host] [port] [options]

Options (defaults match the planned soak run):
    --logins N        sequential logins in phase 1        (default 20)
    --holders N       concurrent holders in phase 2       (default 300)
    --hold S          seconds each holder stays idle       (default 60)
    --ramp-ms MS      stagger between holder connects      (default 10)
    --pid N           server pid for RSS (else LIMBO_PID, else autodetect)
    --max-failures N  tolerated holder/login failures      (default 0)
    --json            append a machine-readable JSON summary

Raw sockets only, stdlib only. Never binds or listens.
"""
import argparse
import errno
import json
import math
import os
import re
import select
import socket
import struct
import subprocess
import sys
import threading
import time

# ---------------------------------------------------------------------------
# Protocol constants (PVN 763 = 1.20/1.20.1, pre-configuration era).
# Mirrors src/protocol/versions.h playIds(763) / playKeepAliveServerbound(763).
# ---------------------------------------------------------------------------
PVN = 763

CB_JOIN_GAME = 0x28   # clientbound Play JoinGame      (era V1_20_0_1)
CB_ABILITIES = 0x34
CB_POSITION = 0x3C
CB_CENTER_CHUNK = 0x4E
CB_CHUNK = 0x24
CB_KEEPALIVE = 0x23   # clientbound Play KeepAlive
CB_DISCONNECT = 0x1A

SB_KEEPALIVE = 0x12   # serverbound Play KeepAlive echo (era V1_20_0_1)

SB_DISCONNECT = 0x00  # login-state disconnect
LOGIN_SUCCESS = 0x02

CONNECT_TIMEOUT = 10.0
IO_TIMEOUT = 10.0
BURST_BUDGET_S = 8.0  # generous: the burst tail can lag well behind JoinGame
MAX_FRAME = 8 << 20   # hard cap on an inbound frame length


# ---------------------------------------------------------------------------
# Wire helpers (same shape as tests/integration/status_ping.py).
# ---------------------------------------------------------------------------
def varint(n):
    u = n & 0xFFFFFFFF
    out = b""
    while True:
        b = u & 0x7F
        u >>= 7
        out += bytes([b | 0x80 if u else b])
        if not u:
            return out


def mcstr(b):
    if isinstance(b, str):
        b = b.encode("utf-8")
    return varint(len(b)) + b


class ProtocolError(Exception):
    """Malformed frame or peer closed mid-packet."""


class Timeout(Exception):
    """Socket deadline expired (distinct from EOF)."""


class Conn:
    """Minimal buffered MC framing reader/writer over a socket."""

    def __init__(self, sock, timeout=IO_TIMEOUT):
        self.sock = sock
        self.sock.settimeout(timeout)
        self.buf = bytearray()

    # -- io ------------------------------------------------------------------
    def _fill(self):
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout:
            raise Timeout("recv timeout")
        except OSError as e:
            raise ProtocolError("recv: %s" % e)
        if not chunk:
            raise ProtocolError("eof")
        self.buf += chunk

    def _exact(self, n):
        while len(self.buf) < n:
            self._fill()
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out

    def _decode_varint(self, data, pos):
        v = shift = 0
        for _ in range(5):
            if pos >= len(data):
                raise ProtocolError("truncated varint")
            b = data[pos]
            pos += 1
            v |= (b & 0x7F) << shift
            if not b & 0x80:
                return v, pos
            shift += 7
        raise ProtocolError("varint overflow")

    # -- frames --------------------------------------------------------------
    def read_varint(self):
        """Read a VarInt that may straddle buffer refills (bytes are consumed)."""
        while True:
            try:
                v, pos = self._decode_varint(bytes(self.buf), 0)
            except ProtocolError as e:
                if "truncated" not in str(e):
                    raise
                self._fill()  # VarInt incomplete: pull more bytes and retry
                continue
            del self.buf[:pos]
            return v

    def read_packet(self):
        """Return (packet_id, body). Raises Timeout / ProtocolError."""
        length = self.read_varint()
        if length <= 0 or length > MAX_FRAME:
            raise ProtocolError("bad frame length %d" % length)
        data = self._exact(length)
        pid, pos = self._decode_varint(data, 0)
        return pid, data[pos:]

    def next_packet(self, timeout=None):
        """Next packet, preferring already-buffered bytes.

        This MUST be used instead of wait_readable()+read_packet(): a peer can
        deliver several packets in one segment, so select() reports "not
        readable" while whole packets are already sitting in self.buf. Blocking
        on select first would then stall until the server sends something else
        and silently truncate the burst.

        timeout=None waits up to IO_TIMEOUT, 0 polls, >0 waits that long.
        Raises Timeout when nothing arrives in time, ProtocolError on EOF.
        """
        if not self.buf:
            if timeout == 0:
                if not self.wait_readable(0.0):
                    raise Timeout("no data")
            else:
                if not self.wait_readable(IO_TIMEOUT if timeout is None else timeout):
                    raise Timeout("no data within %.1fs" % (IO_TIMEOUT if timeout is None else timeout))
        return self.read_packet()

    def send_packet(self, pid, body=b""):
        payload = varint(pid) + body
        try:
            self.sock.sendall(varint(len(payload)) + payload)
        except OSError as e:
            raise ProtocolError("send: %s" % e)

    def send_raw(self, data):
        try:
            self.sock.sendall(data)
        except OSError as e:
            raise ProtocolError("send: %s" % e)

    def wait_readable(self, timeout):
        """True if readable, False on timeout, ProtocolError on peer error."""
        try:
            r, _, _ = select.select([self.sock], [], [], timeout)
        except (OSError, ValueError) as e:
            raise ProtocolError("select: %s" % e)
        if not r:
            return False
        return True

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Server RSS via ps.
# ---------------------------------------------------------------------------
class Rss:
    """Samples the server process RSS in KB through ps(1).

    PID resolution order: --pid / $LIMBO_PID -> the process listening on the
    target port (ss/netstat) -> first process named `limbo`.
    """

    def __init__(self, pid=None, port=None):
        self.pid = pid or int(os.environ.get("LIMBO_PID") or 0) or None
        self.how = "explicit" if self.pid else None
        self.notes = []
        if self.pid is None:
            self.pid = self._pid_on_port(port) or self._autodetect()
        if self.pid is None:
            self.notes.append("server pid not found (use --pid or LIMBO_PID); RSS unavailable")
        elif self.how is None:
            self.how = "port-scan" if port and self._pid_on_port(port) == self.pid else "name-scan"

    @staticmethod
    def _run(cmd):
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=10).stdout.decode("utf-8", "replace")

    @staticmethod
    def _ps_rows():
        """Yield (pid, rss_kb, comm) tuples from ps."""
        try:
            out = subprocess.run(
                ["ps", "-eo", "pid=,rss=,comm="],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10,
            ).stdout.decode("utf-8", "replace")
        except (OSError, subprocess.SubprocessError) as e:
            raise ProtocolError("ps failed: %s" % e)
        for line in out.splitlines():
            parts = line.split(None, 2)
            if len(parts) < 3:
                continue
            try:
                yield int(parts[0]), int(parts[1]), parts[2]
            except ValueError:
                continue

    def _pid_on_port(self, port):
        """Find the PID listening on `port` (ss, then netstat). None if unknown."""
        if not port:
            return None
        for cmd, local_col in ((["ss", "-ltnp"], 3), (["netstat", "-ltnp"], 3)):
            try:
                out = self._run(cmd)
            except (OSError, subprocess.SubprocessError):
                continue
            for line in out.splitlines():
                fields = line.split()
                if len(fields) <= local_col:
                    continue
                if not fields[local_col].endswith(":%d" % port):
                    continue
                m = re.search(r"pid=(\d+)", line) or re.search(r"(\d+)/", line)
                if m:
                    return int(m.group(1))
        return None

    def _autodetect(self):
        try:
            for pid, _rss, comm in self._ps_rows():
                base = os.path.basename(comm)
                if base in ("limbo", "build/limbo") or base.startswith("limbo-"):
                    return pid
        except ProtocolError:
            pass
        return None

    def sample(self):
        """Return (pid, rss_kb) or None when unavailable."""
        if not self.pid:
            return None
        try:
            for pid, rss, _comm in self._ps_rows():
                if pid == self.pid:
                    return pid, rss
        except ProtocolError as e:
            self.notes.append(str(e))
        return None

    @staticmethod
    def _mb(rss_kb):
        return rss_kb / 1024.0


# ---------------------------------------------------------------------------
# Shared soak state.
# ---------------------------------------------------------------------------
class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.hold_start_event = threading.Event()
        self.hold_until = None
        self.results = []
        self.joined_count = 0      # published by holders the moment they are in Play
        self.last_join_at = time.monotonic()
        self.peak_rss = 0
        self.rss_series = []
        self.rss_stop = threading.Event()

    def record(self, entry):
        with self.lock:
            self.results.append(entry)

    def publish_join(self, when):
        """Called by a holder as soon as it reaches the hold phase (not at exit)."""
        with self.lock:
            self.joined_count += 1
            self.last_join_at = max(self.last_join_at, when)

    def joined(self):
        with self.lock:
            return self.joined_count, self.last_join_at


# ---------------------------------------------------------------------------
# Login helpers.
# ---------------------------------------------------------------------------
def connect(host, port, timeout=CONNECT_TIMEOUT):
    last = None
    for family, socktype, proto, _canon, addr in socket.getaddrinfo(
            host, port, socket.AF_UNSPEC, socket.SOCK_STREAM):
        sock = socket.socket(family, socktype, proto)
        try:
            sock.settimeout(timeout)
            sock.connect(addr)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            return sock
        except OSError as e:
            last = e
            try:
                sock.close()
            except OSError:
                pass
    raise last if last else ProtocolError("no address for %s" % host)


def send_handshake(conn, port):
    hs = varint(PVN) + mcstr("localhost") + struct.pack(">H", port) + varint(2)
    conn.send_packet(0x00, hs)


def send_login_start(conn, username):
    # Offline mode: username only; trailing fields (uuid/signature) are ignored.
    conn.send_packet(0x00, mcstr(username))


def drain_play_burst(conn, budget=BURST_BUDGET_S, max_packets=12):
    """Read the post-LoginSuccess Play burst.

    Returns (burst_ids, first_play_id, first_play_monotonic, first_keepalive_body).
    Stops at the void chunk (last burst packet) or when `budget` expires; the
    burst tail can lag behind the first packets, so budget is generous.
    """
    burst = []
    first_id = None
    first_at = None
    ka = None
    deadline = time.monotonic() + budget
    while len(burst) < max_packets:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        try:
            pid, body = conn.next_packet(remaining)
        except (Timeout, ProtocolError):
            break
        burst.append(pid)
        if first_id is None:
            first_id, first_at = pid, time.monotonic()
        if pid == CB_CHUNK:
            break
        if pid == CB_KEEPALIVE and len(body) == 8 and ka is None:
            ka = body
    return burst, first_id, first_at, ka


def echo_keepalive(conn, body):
    conn.send_packet(SB_KEEPALIVE, body)


# ---------------------------------------------------------------------------
# Phase 1: sequential join latency.
# ---------------------------------------------------------------------------
def phase_latency(host, port, count, log):
    samples = []
    print("\n=== phase 1: join latency (%d sequential logins, pvn=%d) ===" % (count, PVN))
    for i in range(count):
        name = "Lat%02d" % i
        entry = {"name": name, "ok": False, "reason": "", "burst": [],
                 "connect_ms": None, "login_ms": None, "first_play_ms": None}
        t0 = time.monotonic()
        sock = None
        conn = None
        try:
            sock = connect(host, port)
            t_conn = time.monotonic()
            conn = Conn(sock)
            send_handshake(conn, port)
            send_login_start(conn, name)

            pid, _body = conn.read_packet()
            t_success = time.monotonic()
            if pid == SB_DISCONNECT:
                entry["reason"] = "login disconnect 0x%02x" % pid
                raise ProtocolError(entry["reason"])
            if pid != LOGIN_SUCCESS:
                entry["reason"] = "expected LoginSuccess 0x02, got 0x%02x" % pid
                raise ProtocolError(entry["reason"])

            burst, first_id, first_at, _ka = drain_play_burst(conn)
            entry.update({
                "ok": first_id is not None,
                "burst": burst,
                "connect_ms": (t_conn - t0) * 1000.0,
                "login_ms": (t_success - t0) * 1000.0,
                "first_play_ms": (first_at - t0) * 1000.0 if first_at is not None else None,
                "first_play_id": first_id,
            })
            if first_id is None:
                entry["reason"] = "no play burst (login ok, nothing in Play)"
            elif first_id != CB_JOIN_GAME:
                entry["ok"] = False
                entry["reason"] = "first play packet was 0x%02x, expected JoinGame 0x%02x" % (
                    first_id if first_id is not None else -1, CB_JOIN_GAME)
        except (ProtocolError, Timeout, OSError) as e:
            if not entry["reason"]:
                entry["reason"] = "%s: %s" % (type(e).__name__, e)
        finally:
            if conn:
                conn.close()
            elif sock:
                try:
                    sock.close()
                except OSError:
                    pass

        samples.append(entry)
        status = "ok" if entry["ok"] else "FAIL(%s)" % entry["reason"]
        if entry["ok"]:
            log("  login %-8s connect=%7.2fms login=%7.2fms first_play=%7.2fms burst=%s"
                % (name, entry["connect_ms"], entry["login_ms"], entry["first_play_ms"],
                   " ".join("0x%02x" % p for p in entry["burst"])))
        else:
            log("  login %-8s %s" % (name, status))

    return samples


# ---------------------------------------------------------------------------
# Phase 2: concurrent idle holders.
# ---------------------------------------------------------------------------
def holder(host, port, index, state, ramp_s):
    """One idle holder: login -> Play burst -> echo KeepAlives -> hold."""
    name = "Soak%04d" % index
    entry = {"name": name, "ok": False, "reason": "", "burst": [],
             "connect_ms": None, "login_ms": None, "first_play_ms": None,
             "keepalives_echoed": 0, "kept_alive": False, "held_s": 0.0}
    if ramp_s:
        time.sleep(ramp_s * index)

    sock = None
    conn = None
    try:
        t0 = time.monotonic()
        sock = connect(host, port)
        t_conn = time.monotonic()
        conn = Conn(sock)
        send_handshake(conn, port)
        send_login_start(conn, name)

        pid, _body = conn.read_packet()
        t_success = time.monotonic()
        if pid != LOGIN_SUCCESS:
            entry["reason"] = "expected LoginSuccess 0x02, got 0x%02x" % pid
            raise ProtocolError(entry["reason"])

        burst, first_id, first_at, ka = drain_play_burst(conn)
        entry.update({
            "burst": burst,
            "connect_ms": (t_conn - t0) * 1000.0,
            "login_ms": (t_success - t0) * 1000.0,
            "first_play_ms": (first_at - t0) * 1000.0 if first_at is not None else None,
            "first_play_id": first_id,
        })
        if first_id is None:
            entry["reason"] = "no play burst (login ok, nothing in Play)"
            raise ProtocolError(entry["reason"])
        if first_id != CB_JOIN_GAME:
            entry["reason"] = "first play packet 0x%02x, expected 0x%02x" % (first_id, CB_JOIN_GAME)
            raise ProtocolError(entry["reason"])
        if ka is not None:
            entry["keepalives_echoed"] += 1
            echo_keepalive(conn, ka)

        entry["ok"] = True
        joined_at = time.monotonic()
        entry["joined_at"] = joined_at
        # Publish immediately: the coordinator waits on this to start the hold,
        # so it must not wait for this holder to finish (it will not for 60s).
        state.publish_join(joined_at)

        # Wait for the whole cohort to be up, echoing KeepAlives meanwhile.
        while not state.hold_start_event.wait(0.05):
            _drain_keepalives(conn, entry, 0.0)

        hold_deadline = state.hold_until
        hold_started = time.monotonic()
        held_to_end = False
        while True:
            now = time.monotonic()
            if now >= hold_deadline:
                held_to_end = True
                break
            try:
                pid, body = conn.next_packet(min(1.0, hold_deadline - now))
            except Timeout:
                continue
            except ProtocolError as e:
                entry["reason"] = "held-connection lost: %s" % e
                entry["ok"] = False
                break
            if pid == CB_KEEPALIVE and len(body) == 8:
                echo_keepalive(conn, body)
                entry["keepalives_echoed"] += 1
            elif pid == CB_DISCONNECT:
                entry["reason"] = "server disconnected during hold"
                entry["ok"] = False
                break

        entry["held_s"] = time.monotonic() - hold_started
        # Only claim the hold if we reached the deadline with the socket alive;
        # a mid-hold drop must not be counted as "survived the full hold".
        entry["kept_alive"] = held_to_end
        if not held_to_end:
            entry["reason"] = entry["reason"] or "hold ended early"
    except (ProtocolError, Timeout, OSError) as e:
        if not entry["reason"]:
            entry["reason"] = "%s: %s" % (type(e).__name__, e)
        if isinstance(e, OSError) and getattr(e, "errno", None) in (errno.ECONNREFUSED, errno.ECONNRESET):
            entry["reason"] = "connect rejected (%s) - server cap/backlog?" % errno.errorcode.get(
                e.errno, e.errno)
    finally:
        if conn:
            conn.close()
        elif sock:
            try:
                sock.close()
            except OSError:
                pass

    state.record(entry)


def _drain_keepalives(conn, entry, budget=0.0):
    """Echo any KeepAlives that are already readable.

    Non-blocking when budget <= 0: used while waiting for the cohort, so a slow
    ramp can never let the server's 30s echo deadline lapse.
    """
    deadline = time.monotonic() + max(0.0, budget)
    while True:
        remaining = deadline - time.monotonic()
        if budget > 0 and remaining <= 0:
            return
        try:
            pid, body = conn.next_packet(remaining if budget > 0 else 0)
        except (Timeout, ProtocolError):
            return
        if pid == CB_KEEPALIVE and len(body) == 8:
            echo_keepalive(conn, body)
            entry["keepalives_echoed"] += 1


def rss_sampler(rss, state, interval):
    while not state.rss_stop.wait(interval):
        s = rss.sample()
        if not s:
            continue
        pid, kb = s
        with state.lock:
            state.rss_series.append((round(time.monotonic(), 2), kb))
            state.peak_rss = max(state.peak_rss, kb)


def phase_holders(host, port, count, hold_s, ramp_ms, rss, log):
    print("\n=== phase 2: %d concurrent idle holders, %ds hold, ramp %.0fms ==="
          % (count, hold_s, ramp_ms))
    state = Stats()
    ramp_s = ramp_ms / 1000.0
    est_span = ramp_s * max(0, count - 1)

    # Start RSS sampling early so "during" covers ramp + hold.
    sampler = None
    if rss.pid:
        sampler = threading.Thread(target=rss_sampler, args=(rss, state, 1.0), daemon=True)
        sampler.start()

    try:
        threading.stack_size(512 * 1024)
    except (ValueError, RuntimeError):
        pass

    t_phase = time.monotonic()
    threads = []
    for i in range(count):
        th = threading.Thread(target=holder,
                              args=(host, port, i, state, ramp_s),
                              name="holder-%d" % i, daemon=True)
        th.start()
        threads.append(th)

    # Watch for completion; the hold starts when the last holder reaches Play.
    deadline = time.monotonic() + max(120.0, 30.0 + est_span)
    while True:
        joined_seen, last_join_at = state.joined()
        if joined_seen >= count:
            break
        if time.monotonic() > deadline:
            log("  ! ramp timeout, starting hold with %d/%d joined" % (joined_seen, count))
            break
        time.sleep(0.05)

    ramp_done = time.monotonic()
    state.hold_until = last_join_at + hold_s
    state.hold_start_event.set()
    log("  ramp complete in %.2fs (%d/%d holders joined); holding %.0fs"
        % (ramp_done - t_phase, joined_seen, count, hold_s))

    for th in threads:
        th.join(timeout=hold_s + 60.0)

    state.rss_stop.set()
    if sampler:
        sampler.join(timeout=5.0)
    log("  all holders closed after %.2fs" % (time.monotonic() - t_phase))
    return state.results, state


# ---------------------------------------------------------------------------
# Reporting.
# ---------------------------------------------------------------------------
def pct(values, p):
    if not values:
        return None
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(math.ceil(p / 100.0 * len(s))) - 1))
    return s[k]


def describe(name, values, unit="ms"):
    # Drop Nones (a metric can be absent when a phase failed part-way).
    values = [v for v in values if v is not None]
    if not values:
        print("  %-22s n=0 (no samples)" % name)
        return {}
    out = {
        "n": len(values),
        "min": min(values),
        "p50": pct(values, 50),
        "p90": pct(values, 90),
        "p99": pct(values, 99),
        "max": max(values),
        "mean": sum(values) / len(values),
    }
    print("  %-22s n=%-4d min=%8.2f%s p50=%8.2f%s p90=%8.2f%s p99=%8.2f%s "
          "max=%8.2f%s mean=%8.2f%s"
          % (name, out["n"], out["min"], unit, out["p50"], unit, out["p90"], unit,
             out["p99"], unit, out["max"], unit, out["mean"], unit))
    return out


def summarize(latency, holders, state, rss, rss_before, rss_after, hold_s):
    ok_lat = [e for e in latency if e["ok"]]
    fail_lat = [e for e in latency if not e["ok"]]
    ok_hold = [e for e in holders if e["ok"]]
    fail_hold = [e for e in holders if not e["ok"]]

    print("\n=== results ===")
    print("phase 1 join latency (total=%d failed=%d)" % (len(latency), len(fail_lat)))
    lat_stats = {
        "connect": describe("connect", [e["connect_ms"] for e in ok_lat]),
        "login_success": describe("login->success", [e["login_ms"] for e in ok_lat]),
        "first_play": describe("join->first play", [e["first_play_ms"] for e in ok_lat]),
    }
    for e in fail_lat[:10]:
        print("  failure: %s: %s" % (e["name"], e["reason"]))
    if len(fail_lat) > 10:
        print("  ... %d more failures" % (len(fail_lat) - 10))

    shapes = {}
    for e in ok_lat:
        shapes[" ".join("0x%02x" % p for p in e["burst"])] = shapes.get(
            " ".join("0x%02x" % p for p in e["burst"]), 0) + 1
    if shapes:
        for shape, n in sorted(shapes.items(), key=lambda kv: -kv[1]):
            print("  burst shape x%-4d %s" % (n, shape))

    print("\nphase 2 idle holders (total=%d failed=%d, hold=%ds)"
          % (len(holders), len(fail_hold), hold_s))
    hold_stats = {
        "connect": describe("connect", [e["connect_ms"] for e in ok_hold]),
        "login_success": describe("login->success", [e["login_ms"] for e in ok_hold]),
        "first_play": describe("join->first play", [e["first_play_ms"] for e in ok_hold]),
    }
    kept = sum(1 for e in holders if e.get("kept_alive"))
    echoed = sum(e["keepalives_echoed"] for e in holders)
    print("  %-22s %d/%d" % ("survived full hold", kept, len(holders)))
    print("  %-22s %d (avg %.2f per holder)" % ("KeepAlives echoed", echoed,
                                               echoed / float(max(1, len(ok_hold)))))
    hshapes = {}
    for e in ok_hold:
        key = " ".join("0x%02x" % p for p in e["burst"])
        hshapes[key] = hshapes.get(key, 0) + 1
    for shape, n in sorted(hshapes.items(), key=lambda kv: -kv[1])[:3]:
        print("  burst shape x%-4d %s" % (n, shape))
    reasons = {}
    for e in fail_hold:
        key = re.sub(r"0x[0-9a-fA-F]+", "0x??", e["reason"])[:80]
        reasons[key] = reasons.get(key, 0) + 1
    for reason, n in sorted(reasons.items(), key=lambda kv: -kv[1])[:10]:
        print("  failure x%-4d %s" % (n, reason))

    rss_stats = {"before": None, "peak": None, "after": None}
    print("\nserver RSS (via ps)")
    if not rss.pid:
        print("  unavailable: %s" % "; ".join(rss.notes))
    else:
        if rss_before:
            rss_stats["before"] = rss_before[1]
            print("  before          pid=%-7d %8.2f MiB (%d kB)"
                  % (rss_before[0], Rss._mb(rss_before[1]), rss_before[1]))
        if state.peak_rss:
            rss_stats["peak"] = state.peak_rss
            print("  during (peak)   pid=%-7d %8.2f MiB (%d kB, %d samples)"
                  % (rss.pid, Rss._mb(state.peak_rss), state.peak_rss,
                     len(state.rss_series)))
        if rss_after:
            rss_stats["after"] = rss_after[1]
            print("  after           pid=%-7d %8.2f MiB (%d kB)"
                  % (rss_after[0], Rss._mb(rss_after[1]), rss_after[1]))
        if rss_before and rss_after:
            delta = rss_after[1] - rss_before[1]
            print("  delta after-before %+8.2f MiB (%+d kB)  <- leak check"
                  % (delta / 1024.0, delta))
            if state.peak_rss:
                print("  delta peak-before %+8.2f MiB (%+d kB)  <- %d idle conns"
                      % ((state.peak_rss - rss_before[1]) / 1024.0,
                         state.peak_rss - rss_before[1], kept))
    for note in rss.notes:
        if "not found" not in note:
            print("  note: %s" % note)

    return {
        "phase1": {
            "total": len(latency), "failed": len(fail_lat), "ok": len(ok_lat),
            "latency_ms": lat_stats,
            "failures": [{"name": e["name"], "reason": e["reason"]} for e in fail_lat],
        },
        "phase2": {
            "total": len(holders), "failed": len(fail_hold), "ok": len(ok_hold),
            "held_full": kept, "keepalives_echoed": echoed,
            "join_ms": hold_stats,
            "failures": [{"name": e["name"], "reason": e["reason"]} for e in fail_hold[:50]],
        },
        "rss_kb": rss_stats,
    }


# ---------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description="limbo soak: join latency + idle holders")
    ap.add_argument("host", nargs="?", default="127.0.0.1")
    ap.add_argument("port", nargs="?", type=int, default=25566)
    ap.add_argument("--logins", type=int, default=20)
    ap.add_argument("--holders", type=int, default=300)
    ap.add_argument("--hold", type=float, default=60.0)
    ap.add_argument("--ramp-ms", type=float, default=10.0)
    ap.add_argument("--pid", type=int, default=None, help="server pid for RSS sampling")
    ap.add_argument("--max-failures", type=int, default=0)
    ap.add_argument("--json", action="store_true", help="print a JSON summary last")
    args = ap.parse_args(argv)

    def log(msg):
        print(msg, flush=True)

    log("limbo soak -> %s:%d (pvn=%d)" % (args.host, args.port, PVN))
    rss = Rss(args.pid, args.port)
    if rss.pid:
        log("server pid=%d (%s)" % (rss.pid, rss.how))
    for note in rss.notes:
        log("note: %s" % note)

    # Pre-flight: fail fast (and cheaply) if nothing is listening.
    try:
        probe = connect(args.host, args.port, timeout=5.0)
        probe.close()
    except OSError as e:
        print("preflight FAILED: cannot connect to %s:%d (%s)" % (args.host, args.port, e),
              file=sys.stderr)
        print("start the server first (the harness never starts one)", file=sys.stderr)
        return 2

    rss_before = rss.sample()

    latency = phase_latency(args.host, args.port, args.logins, log)
    rss_after_p1 = rss.sample()
    if rss_before and rss_after_p1:
        print("  rss after phase 1: %.2f MiB" % Rss._mb(rss_after_p1[1]))

    holders, state = phase_holders(args.host, args.port, args.holders, args.hold,
                                   args.ramp_ms, rss, log)
    rss_after = rss.sample()

    summary = summarize(latency, holders, state, rss, rss_before, rss_after, args.hold)

    failures = summary["phase1"]["failed"] + summary["phase2"]["failed"]
    capped = [e for e in holders if "rejected" in e["reason"] or "no play burst" in e["reason"]]
    print("\n=== verdict ===")
    print("failures: %d (max tolerated %d)" % (failures, args.max_failures))
    if capped:
        print("hint: %d holders never reached Play. tcp_server rejects when "
              "online >= max_players and listen(2) backlog is 128 - raise "
              "max_players in config/limbo.properties (default 100), raise the "
              "backlog, or pass --max-failures %d."
              % (len(capped), len(capped)))
    print("SOAK " + ("PASS" if failures <= args.max_failures else "FAIL"))

    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))

    return 0 if failures <= args.max_failures else 1


if __name__ == "__main__":
    sys.exit(main())
