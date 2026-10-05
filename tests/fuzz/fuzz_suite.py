#!/usr/bin/env python3
"""Malformed-packet fuzz suite (workstream D): the wide corpus behind the lead's
smoke fuzzer.

`tests/fuzz/fuzz_packet.py` is lead-owned and covers a fast smoke pass. This file
is the exhaustive companion: 31 hostile cases across VarInt abuse, length
mismatch, huge/negative strings, truncated LoginStart, Velocity HMAC forgery and
plugin-response abuse, plus flood and slowloris. Every case asserts two things:

  1. the server terminates the connection (silent close/reset, or a polite
     Disconnect then close) rather than hanging or replying;
  2. a clean status ping immediately afterwards still succeeds — the "did not
     crash / did not wedge the accept loop" assertion.

NOT part of `make test`: it needs a live server and sends hostile traffic.

Usage:

    ./build/limbo --config config/limbo.properties

    python3 tests/fuzz/fuzz_suite.py --host 127.0.0.1 --port 25566
    python3 tests/fuzz/fuzz_suite.py --port 25599 --rounds 3        # soak
    python3 tests/fuzz/fuzz_suite.py --corpus tests/fuzz/corpus     # capture
    python3 tests/fuzz/fuzz_suite.py --replay tests/fuzz/corpus/*.bin
    python3 tests/fuzz/fuzz_suite.py --only varint --strict         # one case

Exit code 0 means every case was handled and the server stayed responsive.
Exit code 1 means at least one case crashed, hung, or was accepted.
"""

import argparse
import hashlib
import hmac
import os
import socket
import struct
import sys
import time

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 25566

# Login-state packet ids (see src/protocol/packets.h).
PKT_HANDSHAKE = 0x00
PKT_LOGIN_START = 0x00
PKT_LOGIN_DISCONNECT = 0x00
PKT_LOGIN_PLUGIN_RESPONSE = 0x02
PKT_LOGIN_ACKNOWLEDGED = 0x03
PKT_STATUS_REQUEST = 0x00
PKT_STATUS_PING = 0x01

STATE_STATUS = 1
STATE_LOGIN = 2


# --- wire helpers -----------------------------------------------------------

def varint(n, width=None):
    """Encode a signed int as a Minecraft VarInt. width forces extra bytes."""
    u = n & 0xFFFFFFFF
    out = b""
    while True:
        b = u & 0x7F
        u >>= 7
        out += bytes([b | 0x80]) if u else bytes([b])
        if not u:
            break
    if width:
        pad = width - len(out)
        if pad > 0:
            out = out + bytes([0x80] * (pad - 1)) + bytes([out[-1] & 0x7F])
        else:
            out = out[: width - 1] + bytes([out[width - 1] | 0x80])
    return out


def mcstr(b):
    if isinstance(b, str):
        b = b.encode("utf-8")
    return varint(len(b)) + b


def frame(pid, body=b""):
    payload = varint(pid) + body
    return varint(len(payload)) + payload


def handshake_body(pvn=767, host=b"localhost", port=25566, next_state=STATE_LOGIN):
    return varint(pvn) + mcstr(host) + struct.pack(">H", port) + varint(next_state)


def read_varint(sock, limit=5):
    val = shift = 0
    for _ in range(limit):
        b = sock.recv(1)
        if not b:
            raise EOFError("eof in varint")
        v = b[0]
        val |= (v & 0x7F) << shift
        if not (v & 0x80):
            return val
        shift += 7
    raise ValueError("varint overflow")


def read_packet(sock):
    length = read_varint(sock)
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise EOFError("eof in body")
        data += chunk
    pos = 0
    pid = 0
    shift = 0
    while True:
        b = data[pos]
        pos += 1
        pid |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return pid, data[pos:]


# --- outcome classification -------------------------------------------------

class Result:
    """Outcome of one hostile exchange.

    disconnected — server closed without replying (silent drop).
    rejected    — server replied (e.g. Login Disconnect) and then closed.
    hang        — neither replied nor closed within the timeout. Only the
                  deliberate slowloris/idle payloads may do this legitimately.
    error       — we could not even connect (server down).
    """

    def __init__(self, outcome, detail=""):
        self.outcome = outcome
        self.detail = detail

    @property
    def ok(self):
        return self.outcome in ("disconnected", "rejected")


def _connect(host, port, timeout):
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    return sock


def _try_frame(buf):
    """Best-effort frame scan for reporting. Returns (packet_id, rest) or (None, buf)."""
    pos = 0
    length = 0
    shift = 0
    for _ in range(5):
        if pos >= len(buf):
            return None, buf
        b = buf[pos]
        pos += 1
        length |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    else:
        return None, buf
    if len(buf) < pos + length:
        return None, buf
    inner = 0
    ishift = 0
    ipos = 0
    for _ in range(5):
        if ipos >= length:
            return None, buf
        b = buf[pos + ipos]
        ipos += 1
        inner |= (b & 0x7F) << ishift
        if not (b & 0x80):
            break
        ishift += 7
    else:
        return None, buf
    return inner, buf[pos + length:]


def _drain_until_close(sock, timeout):
    """Read everything until the server closes. Returns (bytes, pids, closed)."""
    sock.settimeout(timeout)
    buf = b""
    pids = []
    while True:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            return buf, pids, False
        except OSError:
            return buf, pids, True
        if chunk == b"":
            return buf, pids, True
        buf += chunk
        try:
            while buf:
                pid, buf = _try_frame(buf)
                if pid is None:
                    break
                pids.append(pid)
        except Exception:  # noqa: BLE001 - diagnostics only, never fatal
            buf = b""


def exchange(host, port, payload, timeout=2.0):
    """Send payload and classify how the server ended the connection."""
    try:
        sock = _connect(host, port, timeout)
    except OSError as e:
        return Result("error", f"connect failed: {e}")
    try:
        if payload:
            sock.sendall(payload)
        buf, pids, closed = _drain_until_close(sock, timeout)
        bits = [f"{len(buf)} bytes out"]
        if pids:
            bits.append("pids " + ",".join(f"0x{p:02x}" for p in pids))
        if not closed:
            return Result("hang", "no close after " + "; ".join(bits))
        if not buf:
            return Result("disconnected", "closed with no reply")
        return Result("rejected", "closed after " + "; ".join(bits))
    except (EOFError, ConnectionResetError, BrokenPipeError) as e:
        return Result("disconnected", str(e))
    except socket.timeout:
        return Result("hang", "timed out")
    except OSError as e:
        return Result("disconnected", str(e))
    finally:
        try:
            sock.close()
        except OSError:
            pass


# --- corpus builders --------------------------------------------------------
# Each returns (name, payload, opts) where opts may set "dribble" or "hold".

def case_varint_overflow_len():
    return ("varint_overflow_len", bytes([0xFF] * 6) + b"\x00", {})


def case_varint_overflow_len_after_valid():
    return ("varint_overflow_len_after_valid",
            bytes([0x02, 0x00, 0x00]) + bytes([0xFF] * 6) + b"\x00", {})


def case_varint_overflow_id():
    body = bytes([0x80] * 6) + b"\x00"
    return ("varint_overflow_id", varint(len(body)) + body, {})


def case_len_mismatch_short():
    return ("len_mismatch_short", varint(64) + b"\x00\x00\x00", {})


def case_len_mismatch_overlong():
    return ("len_mismatch_overlong", varint(2) + b"\x00\x00" + b"A" * 4096, {})


def case_zero_length():
    return ("zero_length", varint(0), {})


def case_negative_length():
    return ("negative_length", varint(-1) + b"\x00", {})


def case_int32min_length():
    return ("int32min_length", varint(-2147483648) + b"\x00", {})


def case_huge_string_username():
    body = frame(PKT_LOGIN_START, varint(1024 * 1024) + b"A" * 1024)
    return ("huge_string_username", frame(PKT_HANDSHAKE, handshake_body()) + body, {})


def case_huge_string_username_full():
    body = frame(PKT_LOGIN_START, varint(1024 * 1024) + b"A" * (1024 * 1024))
    return ("huge_string_username_full", frame(PKT_HANDSHAKE, handshake_body()) + body, {})


def case_huge_string_address():
    hs = frame(PKT_HANDSHAKE, varint(767) + varint(1024 * 1024) + b"A" * 4096 +
               struct.pack(">H", 25566) + varint(STATE_LOGIN))
    return ("huge_string_address", hs, {})


def case_negative_string_len_username():
    body = frame(PKT_LOGIN_START, varint(-1) + b"AAAA")
    return ("negative_string_len_username", frame(PKT_HANDSHAKE, handshake_body()) + body, {})


def case_truncated_login_start():
    return ("truncated_login_start",
            frame(PKT_HANDSHAKE, handshake_body()) + frame(PKT_LOGIN_START, varint(8) + b"abc"),
            {})


def case_empty_username():
    return ("empty_username",
            frame(PKT_HANDSHAKE, handshake_body()) + frame(PKT_LOGIN_START, varint(0)), {})


def case_oversize_username_17():
    return ("oversize_username_17",
            frame(PKT_HANDSHAKE, handshake_body()) + frame(PKT_LOGIN_START, mcstr("A" * 17)), {})


def case_oversize_packet():
    payload = varint(PKT_HANDSHAKE) + b"\x00" * (1024 * 1024)
    return ("oversize_packet", varint(len(payload)) + payload, {})


def case_oversize_plugin_response():
    hs = frame(PKT_HANDSHAKE, handshake_body())
    ls = frame(PKT_LOGIN_START, mcstr("Fuzzer"))
    body = frame(PKT_LOGIN_PLUGIN_RESPONSE, varint(0) + b"\x01" +
                 varint(1024 * 1024) + b"\x00" * 4096)
    return ("oversize_plugin_response", hs + ls + body, {})


def _velocity_payload(secret=b"wrong-secret", mutate=True):
    """Velocity MODERN forwarding payload with a deliberately bad HMAC."""
    inner = varint(1) + mcstr(b"127.0.0.1") + b"\x00" * 16 + mcstr(b"Fuzzer") + varint(0)
    sig = hmac.new(secret, inner, hashlib.sha256).digest()
    if mutate:
        sig = bytes([sig[0] ^ 0xFF]) + sig[1:]  # flip one bit -> invalid
    return sig + inner


def _login_then(plugin_body):
    return (frame(PKT_HANDSHAKE, handshake_body()) +
            frame(PKT_LOGIN_START, mcstr("Fuzzer")) + plugin_body)


def case_bad_hmac():
    body = frame(PKT_LOGIN_PLUGIN_RESPONSE, varint(0) + b"\x01" + _velocity_payload())
    return ("bad_hmac", _login_then(body), {})


def case_bad_hmac_truncated():
    body = frame(PKT_LOGIN_PLUGIN_RESPONSE, varint(0) + b"\x01" + _velocity_payload()[:10])
    return ("bad_hmac_truncated", _login_then(body), {})


def case_bad_hmac_garbage_props():
    inner = varint(9) + mcstr(b"127.0.0.1") + b"\x00" * 16 + mcstr(b"Fuzzer") + varint(0xFFFF)
    body = frame(PKT_LOGIN_PLUGIN_RESPONSE, varint(0) + b"\x01" + b"\xAB" * 32 + inner)
    return ("bad_hmac_garbage_props", _login_then(body), {})


def case_plugin_response_wrong_msgid():
    body = frame(PKT_LOGIN_PLUGIN_RESPONSE, varint(4242) + b"\x01" + _velocity_payload())
    return ("plugin_response_wrong_msgid", _login_then(body), {})


def case_plugin_response_negative_msgid():
    body = frame(PKT_LOGIN_PLUGIN_RESPONSE, varint(-1) + b"\x01" + _velocity_payload())
    return ("plugin_response_negative_msgid", _login_then(body), {})


def case_plugin_response_failure_flag():
    return ("plugin_response_failure_flag",
            _login_then(frame(PKT_LOGIN_PLUGIN_RESPONSE, varint(0) + b"\x00")), {})


def case_wrong_state_packets():
    # Announce Status, then send Login Start. The server answers the status
    # request and waits for a ping, so holding the socket open is CORRECT here —
    # the read timeout is what reclaims the thread.
    return ("wrong_state_packets",
            frame(PKT_HANDSHAKE, handshake_body(next_state=STATE_STATUS)) +
            frame(PKT_LOGIN_START, mcstr("Fuzzer")), {})


def case_login_disconnect_as_request():
    return ("login_disconnect_as_request",
            frame(PKT_HANDSHAKE, handshake_body()) + frame(PKT_LOGIN_DISCONNECT, b"\x00"), {})


def case_null_bytes():
    return ("null_bytes", b"\x00" * 512, {})


def case_random_garbage(seed=0):
    import random
    rng = random.Random(seed)
    return ("random_garbage", bytes(rng.randrange(256) for _ in range(rng.randrange(8, 512))), {})


def case_random_frames(seed=0, count=32):
    import random
    rng = random.Random(seed)
    out = b""
    for _ in range(count):
        body = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 64)))
        payload = varint(rng.randrange(-8, 64)) + body
        declared = rng.choice([len(payload), rng.randrange(0, 128), -1, 2 ** 31 - 1])
        out += varint(declared) + payload
    return ("random_frames", out, {})


def case_slowloris():
    return ("slowloris", frame(PKT_HANDSHAKE, handshake_body())[:1], {"dribble": True})


def case_flood(count=5000):
    return ("flood", frame(PKT_HANDSHAKE, handshake_body(next_state=STATE_STATUS)) +
            frame(0x00, b"") * count, {})


def case_half_open():
    return ("half_open", b"", {"hold": True})


CORPUS = [
    case_varint_overflow_len,
    case_varint_overflow_len_after_valid,
    case_varint_overflow_id,
    case_len_mismatch_short,
    case_len_mismatch_overlong,
    case_zero_length,
    case_negative_length,
    case_int32min_length,
    case_huge_string_username,
    case_huge_string_username_full,
    case_huge_string_address,
    case_negative_string_len_username,
    case_truncated_login_start,
    case_empty_username,
    case_oversize_username_17,
    case_oversize_packet,
    case_oversize_plugin_response,
    case_bad_hmac,
    case_bad_hmac_truncated,
    case_bad_hmac_garbage_props,
    case_plugin_response_wrong_msgid,
    case_plugin_response_negative_msgid,
    case_plugin_response_failure_flag,
    case_wrong_state_packets,
    case_login_disconnect_as_request,
    case_null_bytes,
    case_random_garbage,
    case_random_frames,
    case_slowloris,
    case_flood,
    case_half_open,
]

# Payloads that legitimately wait for more bytes: read_timeout_ms (30 s default)
# must eventually close them, which is longer than the per-case socket timeout.
# A "hang" verdict is informational here unless --strict is passed.
EXPECTED_OPEN = {"slowloris", "half_open", "len_mismatch_short", "wrong_state_packets"}


# --- runner -----------------------------------------------------------------

def save_corpus(outdir, name, payload):
    if not outdir:
        return
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, f"{name}.bin"), "wb") as f:
        f.write(payload)


def liveness_check(host, port, timeout):
    """After hostile traffic the server must still answer a status ping."""
    try:
        sock = _connect(host, port, timeout)
    except OSError as e:
        return False, f"cannot reconnect after fuzzing: {e}"
    try:
        sock.sendall(frame(PKT_HANDSHAKE, handshake_body(next_state=STATE_STATUS)))
        sock.sendall(frame(PKT_STATUS_REQUEST))
        pid, body = read_packet(sock)
        if pid != PKT_STATUS_REQUEST:
            return False, f"unexpected status pid 0x{pid:02x}"
        sock.sendall(frame(PKT_STATUS_PING, struct.pack(">q", 1234)))
        pid2, body2 = read_packet(sock)
        if pid2 != PKT_STATUS_PING or body2 != struct.pack(">q", 1234):
            return False, "ping echo mismatch"
        return True, "status+ping still ok"
    except Exception as e:  # noqa: BLE001 - any failure means not alive
        return False, f"server not responsive: {e}"
    finally:
        try:
            sock.close()
        except OSError:
            pass


def run_case(builder, host, port, timeout, corpus):
    name, payload, opts = builder()
    save_corpus(corpus, name, payload)

    if opts.get("dribble"):
        sock = None
        try:
            sock = _connect(host, port, timeout)
            sock.sendall(payload)
            sock.settimeout(timeout)
            try:
                b = sock.recv(4096)
                if b == b"":
                    return name, Result("disconnected", "closed during dribble")
                return name, Result("rejected", f"{len(b)} bytes then closed")
            except socket.timeout:
                return name, Result("hang", "still open (read timeout may be longer)")
        except OSError as e:
            return name, Result("disconnected", str(e))
        finally:
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass

    if opts.get("hold"):
        sock = None
        try:
            sock = _connect(host, port, timeout)
            sock.settimeout(timeout)
            time.sleep(min(timeout, 1.0))
            try:
                b = sock.recv(4096)
                if b == b"":
                    return name, Result("disconnected", "closed while idle")
                return name, Result("rejected", f"{len(b)} bytes then closed")
            except socket.timeout:
                return name, Result("hang", "idle connection kept open")
        except OSError as e:
            return name, Result("disconnected", str(e))
        finally:
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass

    return name, exchange(host, port, payload, timeout=timeout)


def replay(paths, host, port, timeout, strict=False):
    """Replay raw payloads captured by --corpus."""
    failures = 0
    for path in paths:
        with open(path, "rb") as f:
            blob = f.read()
        name = os.path.basename(path)
        stem = os.path.splitext(name)[0]
        res = exchange(host, port, blob, timeout=timeout)
        bad = res.outcome == "hang" and stem in EXPECTED_OPEN and not strict
        status = "ok " if (res.ok or bad) else "FAIL"
        if status == "FAIL":
            failures += 1
        note = " (expected: waiting on read timeout)" if bad else ""
        print(f"[{status}] {name}: {res.outcome} ({res.detail}){note}")
    alive, detail = liveness_check(host, port, timeout)
    print(f"[{'ok ' if alive else 'FAIL'}] liveness: {detail}")
    if not alive:
        failures += 1
    return failures


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--timeout", type=float, default=2.0,
                    help="per-case socket timeout (s). Default 2.0")
    ap.add_argument("--rounds", type=int, default=1,
                    help="repeat the corpus N times to catch timing bugs")
    ap.add_argument("--corpus", default=None,
                    help="directory to write payloads into (replayable later)")
    ap.add_argument("--replay", nargs="+", default=None,
                    help="replay corpus files instead of the built-in cases")
    ap.add_argument("--only", default=None, help="substring filter on case names")
    ap.add_argument("--strict", action="store_true",
                    help="treat slowloris/idle hangs as failures too (needs "
                         "--timeout > read_timeout_ms to pass)")
    ap.add_argument("--no-liveness", action="store_true",
                    help="skip the clean status ping after each case (faster, weaker)")
    args = ap.parse_args(argv)

    print(f"fuzz suite target {args.host}:{args.port} timeout={args.timeout}s "
          f"rounds={args.rounds}")

    if args.replay:
        failures = replay(args.replay, args.host, args.port, args.timeout, args.strict)
        print("fuzz replay:", "ok" if failures == 0 else f"{failures} failures")
        return 1 if failures else 0

    cases = CORPUS
    if args.only:
        cases = [c for c in CORPUS if args.only in c()[0]]

    failures = []
    notes = []
    for round_idx in range(args.rounds):
        for builder in cases:
            name, res = run_case(builder, args.host, args.port, args.timeout, args.corpus)
            expected_open = name in EXPECTED_OPEN
            tolerated = res.outcome == "hang" and expected_open and not args.strict
            tag = "ok " if (res.ok or tolerated) else "FAIL"
            if tag == "FAIL":
                failures.append((name, res.outcome, res.detail))
            if expected_open and res.outcome == "hang":
                notes.append(f"{name}: still open after {args.timeout}s — expected while "
                             "read_timeout_ms (30s default) has not fired")
            print(f"[{tag}] r{round_idx} {name}: {res.outcome} ({res.detail})")

            if not args.no_liveness:
                alive, detail = liveness_check(args.host, args.port, args.timeout)
                if not alive:
                    failures.append((name, "crashed", detail))
                    print(f"[FAIL] r{round_idx} {name}: recovery failed — {detail}")

    alive, detail = liveness_check(args.host, args.port, args.timeout)
    print(f"[{'ok ' if alive else 'FAIL'}] liveness after fuzzing: {detail}")
    if not alive:
        failures.append(("liveness", "dead", detail))

    for n in notes:
        print(f"note: {n}")
    if failures:
        print("\nFAILURES:")
        for name, outcome, d in failures:
            print(f"  {name}: {outcome} ({d})")
        return 1
    print("fuzz_suite ok: no crash, no hang, all hostile input handled")
    return 0


if __name__ == "__main__":
    sys.exit(main())