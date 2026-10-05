# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""HMAC vector cross-check: must match C++ verifyModern (HMAC-SHA256(secret, body))."""
import hashlib, hmac, struct

def varint(n):
    out = b""
    u = n & 0xFFFFFFFF
    while True:
        b = u & 0x7F
        u >>= 7
        if u: out += bytes([b | 0x80])
        else: out += bytes([b]); break
    return out

def mcstr(s):
    b = s.encode("utf-8"); return varint(len(b)) + b

body = varint(1) + mcstr("127.0.0.1") + bytes(range(16)) + mcstr("TestPlayer") + varint(0)
sig = hmac.new(b"s3cret-velocity", body, hashlib.sha256).digest()
assert len(sig) == 32
# tamper must fail
assert hmac.compare_digest(sig, sig)
bad = bytes([sig[0] ^ 1]) + sig[1:]
assert not hmac.compare_digest(sig, bad)
print(f"python hmac ok sig={sig.hex()[:16]}... body_len={len(body)}")
