# Velocity MODERN Forwarding

> **Sources:** [Velocity docs](https://docs.papermc.io/velocity/player-information-forwarding), [Velocity source](https://github.com/PaperMC/Velocity), [docs/research/limbo-references.md §2](../docs/research/limbo-references.md#2-velocity-modern-forwarding)

---

## Overview

**Velocity MODERN** is the recommended forwarding mode for Velocity 3.0+. It cryptographically verifies player identity using HMAC-SHA256 with a shared secret.

**Why not Legacy/BungeeGuard?**
- Legacy forwarding (`X-Forwarded-For` style headers) is trivially spoofed
- BungeeGuard requires separate plugin and only works for older versions
- MODERN is built into Velocity 3.0+, supports all versions 1.13+ (PVN ≥ 393)

---

## How It Works

```
┌──────────────┐                              ┌──────────────┐
│   CLIENT     │                              │   VELOCITY   │
│  (Vanilla)   │                              │   (Proxy)    │
└──────┬───────┘                              └──────┬───────┘
       │ TLS + Auth                                 │
       │◀──────────────────────────────────────────▶│
       │                                            │
       │         TCP (unencrypted)                  │
       ▼                                            ▼
┌──────────────────────────────────────────────────────────────┐
│                     LIMBO-C++                                │
│  1. Receives LoginStart(username)                            │
│  2. Sends LoginPluginRequest(messageId, "velocity:player_info", "") │
│  3. Receives LoginPluginResponse(messageId, true, HMAC||payload)   │
│  4. Verifies HMAC-SHA256(secret, payload) == HMAC (constant-time)  │
│  5. Parses payload: version, address, uuid, username, props  │
│  6. On success: LoginSuccess(uuid, username) → Play          │
│  7. On failure: Disconnect("Invalid forwarding data")        │
└──────────────────────────────────────────────────────────────┘
```

---

## Configuration

### Velocity `velocity.toml`

```toml
[servers]
limbo = "127.0.0.1:25566"

[player-information-forwarding]
mode = "MODERN"
secret = "a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef"
```

### limbo-c++ `config/limbo.properties`

```properties
forwarding=MODERN
forwarding_secret=a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef
```

**Secret requirements:**
- Exactly 64 hex characters (32 bytes)
- Must match Velocity `secret` exactly (case-insensitive hex)
- Generate with: `openssl rand -hex 32`

---

## HMAC Verification Details

### Algorithm

```python
# Velocity (Java) - PlayerDataForwarding.java
Mac mac = Mac.getInstance("HmacSHA256");
mac.init(new SecretKeySpec(secretBytes, "HmacSHA256"));
byte[] expected = mac.doFinal(payloadBytes);  # 32 bytes
# Sent as: expected || payload
```

```cpp
// limbo-c++ (C++) - velocity.cpp
// Constant-time compare (no early exit)
bool verify(const std::vector<uint8_t>& secret,
            const std::vector<uint8_t>& received) {
    if (received.size() < 32) return false;
    std::array<uint8_t, 32> expected;
    HMAC(EVP_sha256(), secret.data(), secret.size(),
         received.data() + 32, received.size() - 32,
         expected.data(), nullptr);
    return CRYPTO_memcmp(expected.data(), received.data(), 32) == 0;
}
```

### Payload Parsing

```cpp
struct ForwardedPlayer {
    int version;           // 1..4
    std::string address;   // IP, no %zone
    std::array<uint8_t, 16> uuid;
    std::string username;
    std::vector<Property> properties;  // name, value, signed, signature?
};
```

**Version handling:** Accept 1–4. Reject unknown versions with disconnect.

---

## IPv6 Zone ID Stripping

Velocity sends IPv6 addresses with `%zone` suffix (e.g., `2001:db8::1%eth0`). limbo-c++ strips this:

```cpp
// velocity.cpp
std::string stripZoneId(const std::string& addr) {
    size_t pct = addr.find('%');
    return (pct != std::string::npos) ? addr.substr(0, pct) : addr;
}
```

Logged `peerIp` is always clean (no `%zone`, IPv4-mapped normalized to dotted-quad).

---

## Error Handling

| Failure Mode | limbo-c++ Response |
|--------------|-------------------|
| `forwarding=MODERN` but no secret in config | Startup exit(1) |
| Client doesn't support Login Plugin (PVN < 393) | Disconnect: "Velocity forwarding requires 1.13+" |
| `LoginPluginResponse.successful == false` | Disconnect: "Forwarding verification failed" |
| HMAC mismatch (constant-time compare fails) | Disconnect: "Invalid forwarding signature" |
| Payload version not in 1..4 | Disconnect: "Unsupported forwarding version" |
| Payload parse error (truncated, bad string) | Disconnect: "Malformed forwarding data" |
| Velocity sends legacy/BungeeGuard instead | Not handled — disconnect (configure Velocity for MODERN) |

**No fallback to offline-mode.** If MODERN is configured, only MODERN is accepted.

---

## Testing

### HMAC Vector Test (Unit)

```sh
make test
# Runs tests/test_velocity + Python vector check
```

### Integration Test

```sh
# With Velocity running on 25565, limbo on 25566
python3 tests/integration/matrix.py \
    --host 127.0.0.1 --port 25566 \
    --forwarding modern \
    --secret a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef \
    --version 767
```

### Manual Verification

1. Start Velocity with MODERN + secret
2. Start limbo with same secret
3. Connect via vanilla client to Velocity port (25565)
4. Check limbo logs:
```
[limbo] peer=127.0.0.1:xxxxx login plugin request msgId=42
[limbo] peer=127.0.0.1:xxxxx plugin response success=true len=98
[limbo] peer=127.0.0.1:xxxxx forwarded: uuid=... username=Notch addr=127.0.0.1 version=3
[limbo] peer=127.0.0.1:xxxxx login success uuid=... username=Notch
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `forwarding=MODERN requires forwarding_secret` | Secret empty in config | Set `forwarding_secret` (64 hex chars) |
| `forwarding_secret must be 64 hex characters` | Wrong length | `openssl rand -hex 32` |
| "Invalid forwarding signature" | Secret mismatch | Ensure Velocity & limbo secrets identical |
| "Velocity forwarding requires 1.13+" | Client PVN < 393 | Use newer client or disable MODERN |
| Client stuck at "Logging in..." | HMAC mismatch or payload parse | Check logs; verify secret; test with `matrix.py` |
| "Unsupported forwarding version" | Velocity sends version > 4 | Update limbo to accept newer versions |

---

## Security Notes

- **Constant-time compare** (`CRYPTO_memcmp`) prevents timing attacks
- **No fallback** — if MODERN fails, connection is closed (no offline UUID)
- **Secret never logged** — only `peerIp`, `username`, `uuid` (post-verify)
- **Replay protection** — Velocity uses unique `messageId` per request; limbo echoes it
- **IP spoofing** — `address` in payload comes from Velocity (trusted), not client

---

## Migration from Legacy

If currently using `forwarding=LEGACY` (or `NONE` behind Velocity):

1. Generate new secret: `openssl rand -hex 32`
2. Update Velocity `velocity.toml` → `mode = "MODERN"`, `secret = "..."`
3. Update limbo `limbo.properties` → `forwarding=MODERN`, `forwarding_secret=...`
4. Restart both
5. Test with `matrix.py --forwarding modern`

**No client changes needed** — Velocity handles translation.

---

## Related

- [Configuration](Configuration) — all config keys
- [Protocol Support](Protocol-Support) — PVN requirements (PVN ≥ 393)
- [Hardening & Security](Hardening-Security) — rate limits, fuzzing
- [Testing](Testing) — integration matrix