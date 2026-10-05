#!/usr/bin/env python3
"""Version matrix: status + offline login for every era (lead takeover of D remainder).

Expectations match trunk: pre-764 ends in Play burst (JoinGame first),
764+ does RegistryData -> Finish -> ack -> Play burst.
Usage: python3 tests/integration/matrix.py [host] [port]
"""
import socket, struct, sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 25566

PVNS = [47, 335, 338, 340, 393, 401, 404, 477, 480, 481, 482, 485,
        573, 574, 575, 735, 736, 751, 753, 754, 755, 756, 757, 758,
        759, 760, 761, 762, 763, 764, 765, 766, 767, 768, 769, 770,
        771, 772, 773, 774, 775]

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

def rp(s, timeout=8):
    s.settimeout(timeout); ln = rv(s); d = b""
    while len(d) < ln: d += s.recv(ln - len(d))
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

def status_ok(pvn):
    s = socket.create_connection((HOST, PORT), timeout=5)
    sp(s, 0, varint(pvn) + ms(b"localhost") + struct.pack(">H", PORT) + varint(1))
    sp(s, 0, b"")
    pid, body = rp(s)
    assert pid == 0x00, (pvn, hex(pid))
    sp(s, 0x01, struct.pack(">q", pvn))
    pid, body = rp(s)
    assert pid == 0x01 and body == struct.pack(">q", pvn), (pvn, pid)
    s.close()

def login_reaches_play(pvn):
    s = socket.create_connection((HOST, PORT), timeout=5)
    sp(s, 0, varint(pvn) + ms(b"localhost") + struct.pack(">H", PORT) + varint(2))
    sp(s, 0, ms(b"MatrixBot"))
    pid, _ = rp(s)
    assert pid == 0x02, (pvn, "LoginSuccess", hex(pid))
    if pvn >= 764:
        sp(s, 0x03, b"")  # LoginAcknowledged
        fin = 0x02 if pvn < 766 else 0x03
        for _ in range(8):  # RegistryData xN then Finish (we send 1 or 4)
            r, _ = rp(s)
            assert r in (0x05, 0x07, fin), (pvn, hex(r))
            if r == fin:
                break
        else:
            raise RuntimeError("no finish")
        sp(s, 0x02 if pvn < 766 else 0x03, b"")  # Finish ack
    pid2, _ = rp(s, timeout=10)
    s.close()
    return pid2

fails = 0
for pvn in PVNS:
    try:
        status_ok(pvn)
    except Exception as e:
        print(f"pvn={pvn} STATUS FAIL: {e}"); fails += 1; continue
    try:
        first = login_reaches_play(pvn)
        print(f"pvn={pvn} OK status+login firstPlay=0x{first:02x}")
    except Exception as e:
        print(f"pvn={pvn} LOGIN FAIL: {e}"); fails += 1

print("MATRIX " + ("PASS" if fails == 0 else f"FAIL({fails})"))
sys.exit(1 if fails else 0)
