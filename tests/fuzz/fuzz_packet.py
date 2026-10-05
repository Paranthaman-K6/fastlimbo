#!/usr/bin/env python3
"""Malformed-packet fuzzer (lead takeover of D remainder): server must disconnect,
never crash/hang. After each case a clean status ping must succeed (liveness).

Usage: python3 tests/fuzz/fuzz_packet.py [host] [port]
"""
import socket, struct, sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 25566

def varint(n):
    u = n & 0xFFFFFFFF; o = b""
    while True:
        b = u & 0x7F; u >>= 7
        o += bytes([b | 0x80 if u else b])
        if not u: break
    return o

def raw_varint_len(n):  # framed length prefix
    return varint(n)

def ping_ok():
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(5)
    def rv():
        v = sh = 0
        for _ in range(5):
            c = s.recv(1)
            if not c: raise RuntimeError("eof")
            b = c[0]; v |= (b & 0x7F) << sh
            if not b & 0x80: return v
            sh += 7
    def rp():
        ln = rv(); d = b""
        while len(d) < ln: d += s.recv(ln - len(d))
        return d
    def sp(pid, body):
        pl = varint(pid) + body
        s.sendall(varint(len(pl)) + pl)
    def ms(b): return varint(len(b)) + b
    sp(0, varint(763) + ms(b"localhost") + struct.pack(">H", PORT) + varint(1))
    sp(0, b"")
    d = rp()
    s.sendall(varint(1 + 8) + varint(0x01) + struct.pack(">q", 7))
    s.close()

CASES = {
    "varint_overflow": b"\x80\x80\x80\x80\x80\x01",
    "empty_frame": varint(0),
    "huge_len": varint(100000),
    "huge_string_login": varint(10) + varint(0x00) + varint(1000000) + b"A" * 200,
    "login_empty": varint(1) + varint(0x00),
    "handshake_bad_next": varint(763) and None,  # placeholder, built below
    "truncated": varint(50) + b"\x00\x01\x02",
    "garbage": bytes(range(256)),
}

fails = 0
# 1. raw garbage frames
for name, payload in CASES.items():
    if payload is None:
        continue
    try:
        s = socket.create_connection((HOST, PORT), timeout=5)
        s.settimeout(3)
        s.sendall(payload)
        try:
            s.recv(1024)
        except Exception:
            pass
        s.close()
        print(f"case={name} sent (server should drop)")
    except Exception as e:
        print(f"case={name} send-err (ok if refused): {e}")
    try:
        ping_ok()
        print(f"  liveness ok after {name}")
    except Exception as e:
        print(f"  LIVENESS FAIL after {name}: {e}"); fails += 1

# 2. handshake with invalid next-state, then oversize login
try:
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(3)
    hs = varint(763) + varint(9) + b"localhost" + struct.pack(">H", PORT) + varint(99)
    s.sendall(varint(len(varint(0) + hs)) + varint(0) + hs)
    try: s.recv(1024)
    except Exception: pass
    s.close(); print("case=bad_next_state sent")
except Exception as e:
    print(f"case=bad_next_state err: {e}")
try:
    ping_ok(); print("  liveness ok after bad_next_state")
except Exception as e:
    print(f"  LIVENESS FAIL after bad_next_state: {e}"); fails += 1

print("FUZZ " + ("PASS" if fails == 0 else f"FAIL({fails})"))
sys.exit(1 if fails else 0)
