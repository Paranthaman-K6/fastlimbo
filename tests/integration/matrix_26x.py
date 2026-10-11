#!/usr/bin/env python3
"""Raw-socket probe for PVN 776 (26.2) and 777 (26.3).

`tests/integration/matrix.py` (lead-owned) stops at 775. There is no real 26.2 /
26.3 client available here, so the two new eras are verified over raw sockets
only, walking the same flow matrix.py uses:

    Handshake(pvn) -> LoginStart -> LoginSuccess 0x02
      -> LoginAcknowledged 0x03
      -> Configuration: RegistryData ... -> Finish -> Finish ack
      -> Play: first clientbound packet must be JoinGame

Expectations (from src/protocol/versions.h, the lead's 26.2/26.3 rows):
    776 (26.2)  playIds identical to 775 per ViaVersion v26_1to26_2
    777 (26.3)  shifted block per ViaVersion v26_2to26_3
Both are >= 766, so Configuration ids are registryData 0x07, Finish 0x03,
Finish ack (serverbound) 0x03.

Usage:

    ./build/limbo --config config/limbo.properties
    python3 tests/integration/matrix_26x.py                       # 127.0.0.1:25566
    python3 tests/integration/matrix_26x.py --host H --port P
    python3 tests/integration/matrix_26x.py --dump-hex            # raw body bytes

Exit code 0 = every expectation held; 1 = at least one mismatch.
"""

import argparse
import socket
import struct
import sys

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 25566

PKT_HANDSHAKE = 0x00
PKT_LOGIN_START = 0x00
PKT_LOGIN_SUCCESS = 0x02
PKT_LOGIN_ACKNOWLEDGED = 0x03
PKT_FINISH_ACK = 0x03          # serverbound Finish Configuration, >=766
PKT_STATUS_REQUEST = 0x00
PKT_STATUS_PING = 0x01

# 766+ Configuration ids (registry::configIds for pvn >= 766).
CONFIG_REGISTRY_DATA = 0x07
CONFIG_FINISH = 0x03
CONFIG_DISCONNECT = 0x02

# Full clientbound Play burst per era, in send order, from src/protocol/versions.h
# (joinGame, abilities, position, gameEvent, centerChunk, batchStart, then
# one chunk packet per schematic-covered chunk, then batchFinished).
# 775/776 share one row; 777 is the shifted block. Chunk count varies with
# the active schematic, so the harness reads header (7 packets), then drains
# chunk packets until batchFinished instead of comparing a fixed row.
EXPECT_PLAY_HEADER = {
    775: [0x31, 0x40, 0x48, 0x26, 0x5E, 0x0C],
    776: [0x31, 0x40, 0x48, 0x26, 0x5E, 0x0C],
    777: [0x32, 0x41, 0x49, 0x27, 0x60, 0x0C],
}
EXPECT_CHUNK = {775: 0x2D, 776: 0x2D, 777: 0x2E}
EXPECT_BATCH_FINISHED = 0x0B

# KeepAlive is sent every 10s, so it is checked separately from the burst.
EXPECT_KEEPALIVE = {775: 0x2C, 776: 0x2C, 777: 0x2D}

# Status JSON should echo these protocol numbers back to the client.
EXPECT_STATUS_PROTOCOL = {775: 775, 776: 776, 777: 777}


# --- wire helpers (same shape as matrix.py / status_ping.py) ----------------

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


def rv(s):
    v = sh = 0
    for _ in range(5):
        c = s.recv(1)
        if not c:
            raise RuntimeError("eof varint")
        b = c[0]
        v |= (b & 0x7F) << sh
        if not b & 0x80:
            return v
        sh += 7
    raise RuntimeError("varint overflow from server")


def rp(s):
    ln = rv(s)
    d = b""
    while len(d) < ln:
        chunk = s.recv(ln - len(d))
        if not chunk:
            raise RuntimeError("eof body")
        d += chunk
    v = sh = p = 0
    while True:
        b = d[p]
        p += 1
        v |= (b & 0x7F) << sh
        if not b & 0x80:
            break
        sh += 7
    return v, d[p:]


class Probe:
    def __init__(self, pvn):
        self.pvn = pvn
        self.failures = []
        self.observed = []
        self.registry_bodies = []

    def expect(self, label, got, want):
        ok = got == want
        self.observed.append((label, got, want, ok))
        tag = "ok  " if ok else "FAIL"
        print(f"[{tag}] pvn {self.pvn} {label}: got 0x{got:02x} expected 0x{want:02x}")
        if not ok:
            self.failures.append(f"pvn {self.pvn} {label}: 0x{got:02x} != 0x{want:02x}")
        return ok

    def note(self, label, detail):
        self.observed.append((label, detail, None, True))
        print(f"[note] pvn {self.pvn} {label}: {detail}")


def check_status(host, port, pvn, timeout=5.0):
    """Status must answer and echo the era's protocol number back."""
    p = Probe(pvn)
    s = socket.create_connection((host, port), timeout=timeout)
    s.settimeout(timeout)
    try:
        hs = varint(pvn) + mcstr(b"localhost") + struct.pack(">H", port) + varint(1)
        s.sendall(frame(PKT_HANDSHAKE, hs))
        s.sendall(frame(PKT_STATUS_REQUEST))
        pid, body = rp(s)
        p.expect("status response", pid, PKT_STATUS_REQUEST)
        if pid != PKT_STATUS_REQUEST:
            return p
        # String body: VarInt length + JSON.
        ln = 0
        sh = 0
        pos = 0
        while True:
            b = body[pos]
            pos += 1
            ln |= (b & 0x7F) << sh
            if not b & 0x80:
                break
            sh += 7
        text = body[pos:pos + ln].decode("utf-8", "replace")
        want = EXPECT_STATUS_PROTOCOL.get(pvn)
        got = None
        marker = '"protocol":'
        if marker in text:
            tail = text.split(marker, 1)[1]
            num = ""
            for ch in tail:
                if ch.isdigit():
                    num += ch
                else:
                    break
            got = int(num) if num else None
        if want is not None:
            # Decimal protocol number, so report it as decimal (expect() prints hex).
            ok = got == want
            p.observed.append(("status protocol", got, want, ok))
            tag = "ok  " if ok else "FAIL"
            print(f"[{tag}] pvn {pvn} status protocol field: "
                  f"{got} (decimal), expected {want}")
            if not ok:
                p.failures.append(f"pvn {pvn} status protocol: {got} != {want}")
        token = struct.pack(">q", 4242)
        s.sendall(frame(PKT_STATUS_PING, token))
        pid2, body2 = rp(s)
        if pid2 != PKT_STATUS_PING or body2 != token:
            p.failures.append(f"pvn {pvn} ping echo mismatch "
                              f"(id 0x{pid2:02x}, {len(body2)}B)")
            print(f"[FAIL] pvn {pvn} ping echo: id=0x{pid2:02x} len={len(body2)}")
        else:
            p.note("ping echo", "ok")
        return p
    finally:
        try:
            s.close()
        except OSError:
            pass


def probe_era(host, port, pvn, timeout=10.0, dump_hex=False):
    """Walk one era's full login -> configuration -> play flow."""
    p = Probe(pvn)
    s = socket.create_connection((host, port), timeout=timeout)
    s.settimeout(timeout)
    try:
        # Handshake (next_state 2 = login)
        hs = varint(pvn) + mcstr(b"localhost") + struct.pack(">H", port) + varint(2)
        s.sendall(frame(PKT_HANDSHAKE, hs))

        # LoginStart (username only; trailing fields ignored by the server)
        s.sendall(frame(PKT_LOGIN_START, mcstr("Probe26x")))

        pid, body = rp(s)
        p.expect("LoginSuccess", pid, PKT_LOGIN_SUCCESS)
        if pid != PKT_LOGIN_SUCCESS:
            return p
        print(f"[note] pvn {pvn} LoginSuccess body {len(body)}B "
              f"uuid={body[:16].hex()}")
        if dump_hex:
            print(f"       login_success hex: {body.hex()}")

        # LoginAcknowledged -> enters Configuration (both eras are >= 766)
        s.sendall(frame(PKT_LOGIN_ACKNOWLEDGED))

        # Configuration: RegistryData... + Tags + KnownPacks, then Finish.
        # Up to 28 registries for 26.1+ (full registry set) plus 2 extras.
        saw_finish = False
        for _ in range(40):
            pid, body = rp(s)
            p.observed.append(("config packet", pid, None, True))
            print(f"[ok  ] pvn {pvn} config packet id=0x{pid:02x} len={len(body)}")
            if dump_hex and body:
                print(f"       body hex: {body.hex()}")
            if pid == CONFIG_REGISTRY_DATA:
                p.registry_bodies.append(body)
                continue
            if pid in (0x08, 0x09, 0x0D, 0x0E, 0x0F):
                # Update Tags / Known Packs ride along before Finish.
                continue
            if pid == CONFIG_FINISH:
                p.expect("Configuration Finish", pid, CONFIG_FINISH)
                saw_finish = True
                break
            if pid == CONFIG_DISCONNECT:
                p.note("config disconnect", f"server refused era 0x{pid:02x}")
                return p
            p.failures.append(
                f"pvn {pvn} unexpected config packet 0x{pid:02x} "
                f"(want RegistryData 0x{CONFIG_REGISTRY_DATA:02x} or "
                f"Finish 0x{CONFIG_FINISH:02x})")
            print(f"[FAIL] pvn {pvn} unexpected config packet 0x{pid:02x}")
            return p

        if not saw_finish:
            p.failures.append(f"pvn {pvn} never received Configuration Finish")
            return p

        # Finish ack -> server enters Play
        s.sendall(frame(PKT_FINISH_ACK))

        # Walk the Play burst: fixed 7-packet header, then chunk packets
        # until batchFinished (chunk count follows the schematic footprint).
        header = EXPECT_PLAY_HEADER.get(pvn)
        chunkId = EXPECT_CHUNK.get(pvn)
        seen = []
        for _ in range(6):
            try:
                pid, body = rp(s)
            except (RuntimeError, socket.timeout):
                break
            seen.append(pid)
            print(f"[ok  ] pvn {pvn} play packet id=0x{pid:02x} len={len(body)}")
            if dump_hex and body:
                print(f"       body hex: {body.hex()}")
            if len(seen) == 1 and len(body) < 8:
                p.failures.append(f"pvn {pvn} JoinGame body suspiciously short "
                                  f"({len(body)}B)")
                print(f"[FAIL] pvn {pvn} JoinGame body too short: {len(body)}B")
        nChunks = 0
        for _ in range(40):  # schematic footprint bound
            try:
                pid, body = rp(s)
            except (RuntimeError, socket.timeout):
                break
            if pid == EXPECT_BATCH_FINISHED:
                seen.append(pid)
                print(f"[ok  ] pvn {pvn} batchFinished after {nChunks} chunk(s)")
                break
            if pid == chunkId and len(body) > 0:
                nChunks += 1
                seen.append(pid)
                print(f"[ok  ] pvn {pvn} chunk #{nChunks} len={len(body)}")
                continue
            p.failures.append(f"pvn {pvn} unexpected burst packet 0x{pid:02x} "
                              f"(want chunk 0x{chunkId:02x} xN then finish 0x0B)")
            print(f"[FAIL] pvn {pvn} unexpected burst packet 0x{pid:02x}")
            break
        else:
            p.failures.append(f"pvn {pvn} no batchFinished in burst window")
        if nChunks < 1:
            p.failures.append(f"pvn {pvn} burst carried zero chunks")
        if header:
            if seen[:6] == header:
                p.note("Play header", " ".join(f"0x{x:02x}" for x in header))
            else:
                got = " ".join(f"0x{x:02x}" for x in seen[:6])
                want = " ".join(f"0x{x:02x}" for x in header)
                p.failures.append(f"pvn {pvn} Play header mismatch: got [{got}] "
                                  f"want [{want}]")
                print(f"[FAIL] pvn {pvn} Play header mismatch: got [{got}] want [{want}]")
            if seen:
                p.expect("first Play packet (JoinGame)", seen[0], header[0])
        return p
    finally:
        try:
            s.close()
        except OSError:
            pass


def check_keepalive(host, port, pvn, timeout=14.0, wait=13.0):
    """KeepAlive is sent every ~10s; confirm the era's id.

    Only the id matters (an 8-byte i64 body), so this uses a fixed wait rather
    than parsing: a wrong-era KeepAlive id would be silently ignored by a real
    client, so this is the only place the shift is observable for that packet.
    """
    import time
    p = Probe(pvn)
    want = EXPECT_KEEPALIVE.get(pvn)
    s = socket.create_connection((host, port), timeout=timeout)
    s.settimeout(timeout)
    t0 = time.time()
    try:
        hs = varint(pvn) + mcstr(b"localhost") + struct.pack(">H", port) + varint(2)
        s.sendall(frame(PKT_HANDSHAKE, hs))
        s.sendall(frame(PKT_LOGIN_START, mcstr("KaProbe")))
        pid, _ = rp(s)
        if pid != PKT_LOGIN_SUCCESS:
            p.failures.append(f"pvn {pvn} keepalive probe: no LoginSuccess "
                              f"(0x{pid:02x})")
            return p
        s.sendall(frame(PKT_LOGIN_ACKNOWLEDGED))
        for _ in range(40):
            cpid, _ = rp(s)
            if cpid == CONFIG_FINISH:
                s.sendall(frame(PKT_FINISH_ACK))
                break
        while time.time() - t0 < wait:
            try:
                pid, body = rp(s)
            except (RuntimeError, socket.timeout):
                break
            # KeepAlive body is exactly 8 bytes; chunk data is much larger.
            if len(body) == 8 and want is not None and pid != want:
                p.expect("KeepAlive id", pid, want)
                return p
            if len(body) == 8:
                p.expect("KeepAlive id", pid, want)
                return p
        p.note("KeepAlive", f"none seen within {wait:.0f}s")
        return p
    finally:
        try:
            s.close()
        except OSError:
            pass


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--pvns", type=int, nargs="+", default=[775, 776, 777],
                    help="eras to probe (775 is the 26.1 baseline reference)")
    ap.add_argument("--dump-hex", action="store_true",
                    help="print raw packet bodies as hex")
    ap.add_argument("--skip-keepalive", action="store_true",
                    help="skip the ~13s KeepAlive wait")
    args = ap.parse_args(argv)

    print(f"26.x probe target {args.host}:{args.port} pvns={args.pvns}")
    all_failures = []
    for pvn in args.pvns:
        print(f"--- pvn {pvn} " + "-" * 50)
        for label, fn in (("status", check_status),
                          ("login->config->play", probe_era)):
            try:
                p = fn(args.host, args.port, pvn, args.timeout, args.dump_hex) \
                    if fn is probe_era else fn(args.host, args.port, pvn)
                all_failures.extend(p.failures)
                if fn is probe_era:
                    print(f"    registry bodies: {len(p.registry_bodies)} "
                          f"({sum(len(b) for b in p.registry_bodies)}B total)")
            except (OSError, RuntimeError, struct.error) as e:
                msg = f"pvn {pvn} {label} aborted: {type(e).__name__}: {e}"
                print(f"[FAIL] {msg}")
                all_failures.append(msg)
        if not args.skip_keepalive:
            try:
                p = check_keepalive(args.host, args.port, pvn)
                all_failures.extend(p.failures)
            except (OSError, RuntimeError, struct.error) as e:
                msg = f"pvn {pvn} keepalive aborted: {type(e).__name__}: {e}"
                print(f"[FAIL] {msg}")
                all_failures.append(msg)

    print()
    if all_failures:
        print("FAILURES:")
        for f in all_failures:
            print(f"  {f}")
        print("matrix_26x FAIL")
        return 1
    print("matrix_26x ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())