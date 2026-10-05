#!/usr/bin/env python3
"""Hardening matrix: the abuse/abuse-rejection half of workstream D.

`tests/integration/matrix.py` is lead-owned and covers the happy path (status +
offline login across eras). This companion file covers the rejection paths the
acceptance gate calls for and that the lead's matrix does not exercise:

  * bad-secret Velocity MODERN forwarding -> Login Disconnect, never 0x02
  * oversize frame (declared len > max_packet_bytes + 5) -> disconnect
  * 6-byte VarInt overflow in the length prefix -> disconnect
  * post-matrix liveness: a fresh status ping must still work

Both next-state outcomes are accepted for the login check (v0.1 disconnect, or
the merged play burst) so this file works on either endpoint.

NOT part of `make test` — it needs a live server.

Usage:

    ./build/limbo --config config/limbo.properties
    python3 tests/integration/hardening_matrix.py --host 127.0.0.1 --port 25566

    # Velocity MODERN: forge the signature to prove rejection
    python3 tests/integration/hardening_matrix.py --port 25599 --secret wrong-secret

    # also complete the forwarded login to prove the happy path under MODERN
    python3 tests/integration/hardening_matrix.py --port 25599 \
        --secret wrong-secret --real-secret correct-horse-battery-staple

Exit code 0 = all executed checks passed, 1 = at least one failure.
"""

import argparse
import hashlib
import hmac
import json
import socket
import struct
import sys

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 25566
DEFAULT_PVNS = [47, 340, 754, 762, 763, 764, 766, 767]

# Login-state ids (see src/protocol/packets.h).
PKT_HANDSHAKE_ID = 0x00
PKT_LOGIN_START = 0x00
PKT_LOGIN_DISCONNECT = 0x00
PKT_LOGIN_SUCCESS = 0x02
PKT_LOGIN_PLUGIN_RESPONSE = 0x02
PKT_LOGIN_ACKNOWLEDGED = 0x03
PKT_STATUS_REQUEST = 0x00
PKT_STATUS_PING = 0x01

STATE_STATUS = 1
STATE_LOGIN = 2

# proto::playIds() data, mirrored rather than branched inline (versions.h is
# lead-owned; keep the matrix's expectations as data).
PLAY_JOIN_GAME_ID = {47: 0x01, 340: 0x23, 754: 0x24, 762: 0x28, 763: 0x28,
                     764: 0x29, 766: 0x2B, 767: 0x2B}
PLAY_DISCONNECT_ID = {47: 0x40, 340: 0x1A, 754: 0x19, 762: 0x1A, 763: 0x1A,
                      764: 0x1B, 766: 0x1D, 767: 0x1D}
# registry::configIds() / configFinishAckId()
CONFIG_REGISTRY_ID = {764: 0x05, 766: 0x07, 767: 0x07}
CONFIG_FINISH_ID = {764: 0x02, 766: 0x03, 767: 0x03}
CONFIG_DISCONNECT_ID = {764: 0x01, 766: 0x02, 767: 0x02}
CONFIG_FINISH_ACK_SB = {764: 0x02, 766: 0x03, 767: 0x03}


# --- wire helpers (same shape as status_ping.py / ipv6_status.py) ------------

def varint(n):
    u = n & 0xFFFFFFFF
    out = b""
    while True:
        b = u & 0x7F
        u >>= 7
        out += bytes([b | 0x80]) if u else bytes([b])
        if not u:
            return out


def mcstr(b):
    if isinstance(b, str):
        b = b.encode("utf-8")
    return varint(len(b)) + b


def frame(pid, body=b""):
    payload = varint(pid) + body
    return varint(len(payload)) + payload


def handshake_body(pvn, host=b"localhost", port=25566, next_state=STATE_LOGIN):
    return varint(pvn) + mcstr(host) + struct.pack(">H", port) + varint(next_state)


def read_varint(sock):
    v = shift = 0
    for _ in range(5):
        b = sock.recv(1)
        if not b:
            raise EOFError("eof in varint")
        b = b[0]
        v |= (b & 0x7F) << shift
        if not (b & 0x80):
            return v if v < 2**31 else v - 2**32
        shift += 7
    raise ValueError("varint overflow (server sent >5 bytes)")


def read_packet(sock):
    length = read_varint(sock)
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise EOFError("eof in body")
        data += chunk
    pid = 0
    shift = 0
    pos = 0
    while True:
        b = data[pos]
        pos += 1
        pid |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return pid, data[pos:]


def decode_status(body):
    length = 0
    shift = 0
    pos = 0
    while True:
        b = body[pos]
        pos += 1
        length |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return json.loads(body[pos:pos + length].decode("utf-8", "replace"))


def velocity_payload(username, secret):
    """MODERN forwarding payload: HMAC-SHA256 over the inner fields.

    `secret` may be bytes or str (velocity.toml stores it as a plain string,
    hashed as raw UTF-8 bytes).
    """
    if isinstance(secret, str):
        secret = secret.encode("utf-8")
    inner = varint(1) + mcstr("127.0.0.1") + b"\x00" * 16 + mcstr(username) + varint(0)
    return hmac.new(secret, inner, hashlib.sha256).digest() + inner


def connect(host, port, timeout):
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    return sock


class Check:
    def __init__(self, name):
        self.name = name
        self.detail = ""
        self.passed = False
        self.skipped = False

    def ok(self, detail):
        self.passed = True
        self.detail = detail
        return self

    def fail(self, detail):
        self.detail = detail
        return self

    def skip(self, detail):
        self.skipped = True
        self.detail = detail
        return self

    @property
    def verdict(self):
        if self.passed:
            return "PASS"
        return "SKIP" if self.skipped else "FAIL"


# --- checks -----------------------------------------------------------------

def check_status(host, port, pvn, timeout):
    """Status response 0x00 with valid JSON, then ping echo 0x01."""
    c = Check(f"pvn {pvn} status")
    try:
        sock = connect(host, port, timeout)
    except OSError as e:
        return c.fail(f"connect: {e}")
    try:
        sock.sendall(frame(PKT_HANDSHAKE_ID,
                           handshake_body(pvn, next_state=STATE_STATUS)))
        sock.sendall(frame(PKT_STATUS_REQUEST))
        pid, body = read_packet(sock)
        if pid != PKT_STATUS_REQUEST:
            return c.fail(f"expected status 0x00, got 0x{pid:02x}")
        status = decode_status(body)
        if "players" not in status or "version" not in status:
            return c.fail(f"bad status json: {status}")
        token = struct.pack(">q", 0x0123456789ABCDEF)
        sock.sendall(frame(PKT_STATUS_PING, token))
        pid2, body2 = read_packet(sock)
        if pid2 != PKT_STATUS_PING or body2 != token:
            return c.fail(f"ping echo mismatch pid=0x{pid2:02x}")
        return c.ok(f"proto={status['version']['protocol']} "
                    f"players={status['players']['online']}/{status['players']['max']}")
    except (EOFError, ValueError, OSError, IndexError) as e:
        return c.fail(f"{type(e).__name__}: {e}")
    finally:
        try:
            sock.close()
        except OSError:
            pass


def _finish_login(c, sock, pvn, has_config, prefix):
    """After LoginSuccess, walk the version-correct next state.

    Both legal outcomes PASS: the v0.1 endpoint disconnect (login proof only) and
    the merged play burst (JoinGame, via Configuration for 764+). Anything else
    fails.
    """
    if not has_config:
        pid, _ = read_packet(sock)
        if pid == PLAY_JOIN_GAME_ID.get(pvn):
            return c.ok(f"{prefix}LoginSuccess -> Play JoinGame 0x{pid:02x}")
        if pid == PLAY_DISCONNECT_ID.get(pvn):
            return c.ok(f"{prefix}LoginSuccess -> Play Disconnect 0x{pid:02x} "
                        f"(v0.1 endpoint, by design)")
        return c.fail(f"expected Play JoinGame 0x{PLAY_JOIN_GAME_ID.get(pvn, 0):02x} "
                      f"or Disconnect 0x{PLAY_DISCONNECT_ID.get(pvn, 0):02x}, "
                      f"got 0x{pid:02x}")

    sock.sendall(frame(PKT_LOGIN_ACKNOWLEDGED))
    registry_id = CONFIG_REGISTRY_ID.get(pvn)
    finish_id = CONFIG_FINISH_ID.get(pvn)
    ack_sb = CONFIG_FINISH_ACK_SB.get(pvn)
    disc_id = CONFIG_DISCONNECT_ID.get(pvn)
    saw_registry = False
    for _ in range(16):
        pid, _body = read_packet(sock)
        if pid == disc_id:
            return c.ok(f"{prefix}ack 0x03 -> "
                        f"{'RegistryData -> ' if saw_registry else ''}"
                        f"Config Disconnect 0x{pid:02x} (v0.1 endpoint, by design)")
        if pid == registry_id:
            saw_registry = True
            continue
        if pid == finish_id:
            sock.sendall(frame(ack_sb))
            pid2, _ = read_packet(sock)
            if pid2 == PLAY_JOIN_GAME_ID.get(pvn):
                return c.ok(f"{prefix}ack 0x03 -> RegistryData -> Finish"
                            f"(0x{finish_id:02x}) -> ack -> Play JoinGame 0x{pid2:02x}")
            if pid2 == PLAY_DISCONNECT_ID.get(pvn):
                return c.ok(f"{prefix}ack 0x03 -> Finish(0x{finish_id:02x}) -> Play "
                            f"Disconnect 0x{pid2:02x} (v0.1 endpoint, by design)")
            return c.fail(f"after Finish ack expected JoinGame "
                          f"0x{PLAY_JOIN_GAME_ID.get(pvn, 0):02x} or Disconnect "
                          f"0x{PLAY_DISCONNECT_ID.get(pvn, 0):02x}, got 0x{pid2:02x}")
        return c.fail(f"unexpected config packet 0x{pid:02x} (want RegistryData "
                      f"0x{registry_id:02x} / Finish 0x{finish_id:02x} / "
                      f"Disconnect 0x{disc_id:02x})")
    return c.fail("configuration never finished within 16 packets")


def check_login(host, port, pvn, timeout, username, real_secret=None):
    """Login reaches LoginSuccess 0x02, then the version-correct next state."""
    c = Check(f"pvn {pvn} login")
    has_config = pvn >= 764
    try:
        sock = connect(host, port, timeout)
    except OSError as e:
        return c.fail(f"connect: {e}")
    try:
        sock.sendall(frame(PKT_HANDSHAKE_ID, handshake_body(pvn)))
        sock.sendall(frame(PKT_LOGIN_START, mcstr(username)))
        pid, body = read_packet(sock)
        if pid == 0x04:  # LoginPluginRequest -> forwarding is mandatory
            if real_secret is None:
                return c.skip("server is forwarding=MODERN; pass --real-secret to "
                              "complete the forwarding handshake")
            payload = velocity_payload(username, real_secret)
            sock.sendall(frame(PKT_LOGIN_PLUGIN_RESPONSE, varint(0) + b"\x01" + payload))
            pid, body = read_packet(sock)
            if pid != PKT_LOGIN_SUCCESS:
                return c.fail(f"forwarded login expected LoginSuccess 0x02, "
                              f"got 0x{pid:02x}")
            return _finish_login(c, sock, pvn, has_config, "Velocity MODERN ok, ")
        if pid != PKT_LOGIN_SUCCESS:
            return c.fail(f"expected LoginSuccess 0x02, got 0x{pid:02x} ({len(body)}B)")
        if len(body) < 18:
            return c.fail(f"LoginSuccess body too short ({len(body)}B)")
        return _finish_login(c, sock, pvn, has_config, "")
    except (EOFError, ValueError, OSError, IndexError) as e:
        return c.fail(f"{type(e).__name__}: {e}")
    finally:
        try:
            sock.close()
        except OSError:
            pass


def check_bad_secret(host, port, pvn, timeout, secret, username):
    """Velocity MODERN with a wrong secret must be rejected, never admitted."""
    c = Check(f"pvn {pvn} bad-secret velocity reject")
    try:
        sock = connect(host, port, timeout)
    except OSError as e:
        return c.fail(f"connect: {e}")
    try:
        sock.sendall(frame(PKT_HANDSHAKE_ID, handshake_body(pvn)))
        sock.sendall(frame(PKT_LOGIN_START, mcstr(username)))
        pid, _ = read_packet(sock)
        if pid == 0x04:
            payload = velocity_payload(username, secret)  # forged signature
            sock.sendall(frame(PKT_LOGIN_PLUGIN_RESPONSE, varint(0) + b"\x01" + payload))
            pid2, _ = read_packet(sock)
            if pid2 == PKT_LOGIN_SUCCESS:
                return c.fail("server accepted a forged HMAC — security hole")
            if pid2 != PKT_LOGIN_DISCONNECT:
                return c.fail(f"expected Login Disconnect 0x00, got 0x{pid2:02x}")
            return c.ok("LoginPluginRequest -> forged HMAC -> Login Disconnect 0x00")
        if pid == PKT_LOGIN_SUCCESS:
            return c.skip("server is forwarding=NONE; start it with "
                          "forwarding=MODERN + forwarding_secret to exercise this")
        return c.fail(f"unexpected first reply 0x{pid:02x}: cannot test bad secret")
    except (EOFError, ValueError, OSError, IndexError) as e:
        return c.fail(f"{type(e).__name__}: {e}")
    finally:
        try:
            sock.close()
        except OSError:
            pass


def _expect_disconnect(c, host, port, label, timeout, send):
    """Assert the server terminates the connection after `send(sock)`."""
    try:
        sock = connect(host, port, timeout)
    except OSError as e:
        return c.fail(f"connect: {e}")
    try:
        send(sock)
        sock.settimeout(timeout)
        try:
            data = sock.recv(4096)
        except socket.timeout:
            return c.fail(f"{label}: connection kept open (no disconnect)")
        except (ConnectionResetError, BrokenPipeError):
            return c.ok(f"{label}: connection reset (disconnected)")
        if data == b"":
            return c.ok(f"{label}: closed")
        return c.fail(f"{label}: server replied with {len(data)} bytes instead of closing")
    except (ConnectionResetError, BrokenPipeError):
        # Reset can surface on send() rather than recv() depending on timing.
        return c.ok(f"{label}: connection reset (disconnected)")
    except OSError as e:
        return c.fail(f"{label}: {type(e).__name__}: {e}")
    finally:
        try:
            sock.close()
        except OSError:
            pass


def check_oversize(host, port, pvn, timeout, max_packet_bytes=8192):
    """Declared frame length above max_packet_bytes + 5 must close the socket."""
    c = Check(f"pvn {pvn} oversize disconnect")
    declared = max_packet_bytes + 6  # one byte past the reader's cap

    def send(sock):
        sock.sendall(varint(declared) + b"\x00" * 16)
        sock.sendall(frame(PKT_HANDSHAKE_ID, handshake_body(pvn, port=port)))

    return _expect_disconnect(c, host, port,
                              f"declared len {declared} > {max_packet_bytes}+5",
                              timeout, send)


def check_varint_overflow(host, port, pvn, timeout):
    """Six continuation bytes in the length prefix must not wedge the server."""
    c = Check(f"pvn {pvn} varint overflow disconnect")

    def send(sock):
        sock.sendall(bytes([0xFF] * 6) + b"\x00")

    return _expect_disconnect(c, host, port, "6-byte VarInt length", timeout, send)


def check_huge_string(host, port, pvn, timeout):
    """A 1 MiB string field must be refused, never allocated.

    Two acceptable reactions: a Login Disconnect 0x00 followed by close (the
    server parsed the length prefix and refused the field), or a silent close.
    A *successful* login here would be the bug this check exists to catch.
    """
    c = Check(f"pvn {pvn} 1MiB string disconnect")
    try:
        sock = connect(host, port, timeout)
    except OSError as e:
        return c.fail(f"connect: {e}")
    try:
        sock.sendall(frame(PKT_HANDSHAKE_ID, handshake_body(pvn, port=port)))
        sock.sendall(frame(PKT_LOGIN_START, varint(1024 * 1024) + b"A" * 1024))
        pid, body = read_packet(sock)
        if pid == PKT_LOGIN_SUCCESS:
            return c.fail("server accepted a 1 MiB username field")
        if pid != PKT_LOGIN_DISCONNECT:
            return c.fail(f"expected Login Disconnect 0x00, got 0x{pid:02x}")
        return c.ok(f"1 MiB username field refused: Login Disconnect 0x00 "
                    f"({len(body)}B) + close")
    except EOFError:
        return c.ok("1 MiB username field refused: closed with no reply")
    except (ValueError, OSError, IndexError) as e:
        return c.fail(f"{type(e).__name__}: {e}")
    finally:
        try:
            sock.close()
        except OSError:
            pass


# --- runner -----------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--timeout", type=float, default=5.0)
    ap.add_argument("--pvns", type=int, nargs="+", default=DEFAULT_PVNS)
    ap.add_argument("--secret", default=None,
                    help="wrong secret for the forged-HMAC check")
    ap.add_argument("--real-secret", default=None,
                    help="correct secret so the login check also runs under MODERN")
    ap.add_argument("--username", default="HardeningBot")
    ap.add_argument("--max-packet-bytes", type=int, default=8192,
                    help="must match config max_packet_bytes (default 8192)")
    args = ap.parse_args(argv)

    print(f"hardening matrix target {args.host}:{args.port} pvns={args.pvns} "
          f"timeout={args.timeout}s")

    checks = []
    for pvn in args.pvns:
        checks.append(check_status(args.host, args.port, pvn, args.timeout))
        checks.append(check_login(args.host, args.port, pvn, args.timeout,
                                  args.username, args.real_secret))
        checks.append(check_varint_overflow(args.host, args.port, pvn, args.timeout))
        checks.append(check_oversize(args.host, args.port, pvn, args.timeout,
                                     args.max_packet_bytes))
        checks.append(check_huge_string(args.host, args.port, pvn, args.timeout))
        if args.secret is not None:
            checks.append(check_bad_secret(args.host, args.port, pvn, args.timeout,
                                           args.secret, args.username))

    print()
    print(f"{'result':6} {'check':38} detail")
    for c in checks:
        print(f"{c.verdict:6} {c.name:38} {c.detail}")

    alive = check_status(args.host, args.port, args.pvns[-1], args.timeout)
    print(f"{alive.verdict:6} {'post-matrix liveness':38} {alive.detail}")

    failed = [c for c in checks if not c.passed and not c.skipped]
    passed = [c for c in checks if c.passed]
    skipped = [c for c in checks if c.skipped]
    print()
    print(f"hardening matrix: {len(passed)} passed, {len(skipped)} skipped, "
          f"{len(failed)} failed")
    if failed:
        print("FAILURES:")
        for c in failed:
            print(f"  {c.name}: {c.detail}")
        return 1
    print("hardening matrix ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())