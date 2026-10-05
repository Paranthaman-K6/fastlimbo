# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""Raw-socket integration: status ping + offline login against live limbo (v0.1)."""
import socket, struct, sys, json

HOST, PORT = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1", int(sys.argv[2]) if len(sys.argv) > 2 else 25566

def varint(n):
    u = n & 0xFFFFFFFF; out = b""
    while True:
        b = u & 0x7F; u >>= 7
        if u: out += bytes([b | 0x80])
        else: out += bytes([b]); break
    return out

def read_varint(s):
    v = shift = 0
    for _ in range(5):
        b = s.recv(1)
        if not b: raise RuntimeError("eof")
        b = b[0]; v |= (b & 0x7F) << shift
        if not (b & 0x80): return v if v < 2**31 else v - 2**32
        shift += 7
    raise RuntimeError("varint overflow")

def read_packet(s):
    ln = read_varint(s)
    data = b""
    while len(data) < ln:
        c = s.recv(ln - len(data))
        if not c: raise RuntimeError("eof body")
        data += c
    # split id
    v = shift = 0; pos = 0
    for _ in range(5):
        b = data[pos]; pos += 1; v |= (b & 0x7F) << shift
        if not (b & 0x80): break
        shift += 7
    return v, data[pos:]

def send_packet(s, pid, body):
    payload = varint(pid) + body
    s.sendall(varint(len(payload)) + payload)

def mcstr(s): return varint(len(s)) + s

# --- status ---
s = socket.create_connection((HOST, PORT), timeout=5)
hs = varint(767) + mcstr(b"localhost") + struct.pack(">H", PORT) + varint(1)
send_packet(s, 0x00, hs)
send_packet(s, 0x00, b"")
pid, body = read_packet(s)
assert pid == 0x00, pid
# body = VarInt len + JSON
ln = 0; shift = 0; pos = 0
while True:
    b = body[pos]; pos += 1; ln |= (b & 0x7F) << shift
    if not (b & 0x80): break
    shift += 7
status = json.loads(body[pos:pos+ln].decode())
assert "players" in status and "version" in status, status
print("status ok:", status["version"], status["description"])
ping_token = struct.pack(">q", 123456789)
send_packet(s, 0x01, ping_token)
pid, body = read_packet(s)
assert pid == 0x01 and body == ping_token, (pid, body)
s.close()
print("ping ok")

# --- login (offline, pre-config version 1.20.1/763) ---
s = socket.create_connection((HOST, PORT), timeout=5)
hs = varint(763) + mcstr(b"localhost") + struct.pack(">H", PORT) + varint(2)
send_packet(s, 0x00, hs)
send_packet(s, 0x00, mcstr(b"TestPlayer"))
pid, body = read_packet(s)
print(f"login flow first reply pid=0x{pid:02x} len={len(body)}")
assert pid == 0x02, f"expected LoginSuccess 0x02, got 0x{pid:02x}"
# next should be Play Disconnect (v0.1 endpoint)
pid2, body2 = read_packet(s)
print(f"second reply pid=0x{pid2:02x} len={len(body2)} (expected play disconnect)")
s.close()
print("integration ok")
