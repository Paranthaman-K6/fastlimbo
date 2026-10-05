# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""IPv6 integration: status ping over ::1 against live limbo (dual-stack socket)."""
import socket, struct, sys, json

HOST = sys.argv[1] if len(sys.argv) > 1 else "::1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 25566

def varint(n):
    u = n & 0xFFFFFFFF; o = b""
    while True:
        b = u & 0x7F; u >>= 7
        o += bytes([b | 0x80 if u else b])
        if not u: break
    return o

def rv(s):
    v = sh = 0
    for _ in range(5):
        c = s.recv(1)
        if not c: raise RuntimeError("eof varint")
        b = c[0]; v |= (b & 0x7F) << sh
        if not b & 0x80: return v
        sh += 7
    raise RuntimeError("overflow")

def rp(s):
    ln = rv(s); d = b""
    while len(d) < ln:
        c = s.recv(ln - len(d))
        if not c: raise RuntimeError("eof body")
        d += c
    v = sh = p = 0
    for _ in range(5):
        b = d[p]; p += 1; v |= (b & 0x7F) << sh
        if not b & 0x80: break
        sh += 7
    return v, d[p:]

def sp(s, pid, body):
    pl = varint(pid) + body
    s.sendall(varint(len(pl)) + pl)

def ms(b): return varint(len(b)) + b

s = socket.create_connection((HOST, PORT), timeout=5)
print(f"connected via {s.getsockname()} -> {s.getpeername()}")
hs = varint(767) + ms(b"localhost") + struct.pack(">H", PORT) + varint(1)
sp(s, 0x00, hs)
sp(s, 0x00, b"")
pid, body = rp(s)
assert pid == 0x00, pid
ln = sh = pos = 0
while True:
    b = body[pos]; pos += 1; ln |= (b & 0x7F) << sh
    if not b & 0x80: break
    sh += 7
status = json.loads(body[pos:pos+ln].decode())
print("ipv6 status ok:", status["version"])
sp(s, 0x01, struct.pack(">q", 42))
pid, body = rp(s)
assert pid == 0x01 and body == struct.pack(">q", 42)
s.close()
print("ipv6 integration ok")
