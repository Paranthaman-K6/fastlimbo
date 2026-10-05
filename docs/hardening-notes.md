# Hardening notes — limits, caps, fuzz corpus, integration matrix

Workstream D (hardening + tests). Everything here is additive: no file in
`src/server/`, `src/protocol/play.*`, `src/protocol/registry.*` or `src/world/`
was touched, and the `Makefile` is unchanged. `make test` stays green.

New files:

| file | owner | purpose |
| --- | --- | --- |
| `src/security/limits.h` / `.cpp` | D | token-bucket rate limiter + size-cap helpers, pure logic |
| `tests/test_limits.cpp` | D | unit tests with an injected clock (no sleeping) |
| `tests/fuzz/fuzz_suite.py` | D | 31-case malformed-packet suite, per-case recovery check |
| `tests/integration/hardening_matrix.py` | D | rejection paths: bad secret, oversize, VarInt overflow, 1 MiB string |
| `docs/hardening-notes.md` | D | this file |
| `tests/integration/matrix.py` | lead | happy-path handshake matrix (taken over from D) |
| `tests/fuzz/fuzz_packet.py` | lead | smoke fuzzer (taken over from D) |

Note on ownership: the lead rewrote `tests/integration/matrix.py` and
`tests/fuzz/fuzz_packet.py` mid-flight ("lead takeover of D remainder"), trimming
them to a fast smoke pass. Rather than fight those edits, the remaining coverage
lives in `hardening_matrix.py` and `fuzz_suite.py`, which are supersets of the
originals and still accept both endpoints. Nothing was lost.

---

## 1. Limits chosen

Two independent token buckets per connection (`security::Limiter`), default
values picked from the NanoLimbo/PicoLimbo traffic comments plus the observed
vanilla client profile (20-60 packets/s while moving, single-digit while idle):

| dimension | default | burst | why |
| --- | --- | --- | --- |
| packets/sec | `500.0` | `500` (= 1 s of traffic) | ~10-25x above a real client; catches flood loops while never touching a legitimate burst (chunk flush + keepalive + teleport-confirm land together) |
| bytes/sec | `2048.0 * 8.0` = `16384` | `16384` | matches the `2048*8` traffic note; ~16 KB/s inbound is far above a limbo client's real inbound volume (movement + keepalive are tens of bytes/s) |
| maxBytesPerCall | `8192` | n/a | matches `max_packet_bytes`; a single larger frame is refused outright so one packet cannot drain a full bucket in one call |

Behaviour details that the unit tests pin down (`tests/test_limits.cpp`):

- **Burst is allowed.** 500 packets of 1 byte pass instantly; 2 packets of
  8192 bytes pass instantly (byte burst exactly consumed).
- **Sustained flood is rejected.** 10 000 packets of 8 bytes with no time
  advance: exactly 500 accepted, 9500 refused. With 8192-byte packets the byte
  bucket binds first and only 2 pass.
- **Refill over time is driven by an injected clock.** +100 ms at 500 pkt/s
  restores 50 tokens; waiting 60 s never exceeds the burst (still 500). The
  byte bucket refills at 16384 B/s: 1 s restores one full burst.
- **A refusal charges nothing.** Byte-exhausted connections still keep their
  packet budget (`testNoChargeOnRefusal`), so one abusive stream cannot also
  consume the packet dimension.
- **A backwards clock never drains.** `TokenBucket::update` ignores a
  non-monotonic instant instead of treating it as elapsed time; the next
  forward sample resumes from the stored baseline and is capped at burst.
- **`rate <= 0` disables that dimension** (useful when a lead wants packets
  limited but bytes unbounded, or vice versa).
- **Real traffic never trips.** 120 s at 20 pkt/s x 512 B and 60 s at
  60 pkt/s x 200 B both pass untouched.

### Size caps (`security::SizeCaps` + `*LenOk` helpers)

| helper | cap | rejects |
| --- | --- | --- |
| `stringLenOk(len, cap)` | 32767 | negatives, over-cap |
| `usernameLenOk(len)` | 16 | negatives, 17+ |
| `addressLenOk(len)` | 255 | negatives, 256+ |
| `arrayLenOk(len)` | 262144 (256 KiB) | negatives, larger blobs |
| `arrayCountOk(count)` | 65536 | negatives, larger element counts |
| `frameLenOk(len, maxBytes)` | `max_packet_bytes`, hard-clamped to 1 MiB | `len == 0` (no room for a packet id), negatives, over-cap |

All of these are pure integer comparisons — no allocation, so a hostile length
prefix can never become a huge `reserve()`.

---

## 2. Wiring status

`connection.cpp` is lead-owned and has since been wired up: `readPacket` now
takes an optional `security::Limiter*` and returns `false` when `take()` refuses
(`src/server/connection.cpp:50-64`), and `handleConnection` constructs one
`Limiter` per connection from `cfg.maxPacketBytes`
(`src/server/connection.cpp:153-156`). So the hook below is no longer pending:

```cpp
// connection.cpp:64 — the actual call site
if (lim && !lim->take(len)) return false;  // flood guard: drop abusive connection
```

Two things worth confirming on the lead's side:

1. `Limiter::take(len)` charges the **whole declared frame length**, which is
   the byte-accounting the limits were sized for. Good.
2. Because the limiter charges inside `readPacket`, a refused packet makes
   `readPacket` return `false`, which every call site currently treats as a
   transport error (silent `close(fd)`). That is the intended "disconnect on
   abuse" behaviour, but in the **Login** state a polite `Login Disconnect`
   would be friendlier to real clients whose proxies burst on join. Optional
   polish, not a correctness issue — the fuzz suite and hardening matrix both
   accept either "silent close" or "Disconnect then close".

The original intended hook, for reference:

```cpp
// in handleConnection(), one Limiter per connection, created at the top:
//   security::Limiter limiter(security::LimiterConfig{
//       /*packetsPerSecond=*/500.0,
//       /*bytesPerSecond=*/2048.0 * 8.0,
//       /*packetBurst=*/0.0, /*byteBurst=*/0.0, /*maxBytesPerCall=*/8192});

if (!readPacket(fd, cfg.readTimeoutMs, cfg.maxPacketBytes, id, body)) { close(fd); return; }

// after the frame is parsed, before any state handling:
if (!limiter.take(body.size() + 3)) {          // +3 ~= id VarInt + slack
  // abusive rate: drop. In Login state a Login Disconnect is friendlier.
  if (!loggedIn) writeAll(fd, proto::framePacket(0x00, packets::loginDisconnectBody(pvn, "Kicked by server")));
  close(fd);
  return;
}
```

Design notes that still matter (the shipped call site matches these):

1. **One `Limiter` per connection**, constructed inside `handleConnection`, so
   the default `steadySeconds()` clock gives per-connection state with no locking.
2. **Charge on every parsed frame**, in all three states. Status probes are
   cheap and login is one packet; the Play `KeepAlive` loop is where flood
   protection actually bites, and the lead's `enterPlay` already passes the
   limiter through (`connection.cpp:135`).
3. **Charge the declared frame length** (`len`), not `body.size()`. The reader
   knows `len` before allocating, so the charge cannot be evaded by lying about
   the body.
4. **Disconnect on refusal**, not a silent stall: the fuzz suite and both matrix
   scripts treat "closed" as the pass condition.
5. Still unused by the trunk, available if wanted:
   `security::frameLenOk(len, cfg.maxPacketBytes)` to replace the ad-hoc
   `len > maxBytes + 5` in `readPacket`, and `security::usernameLenOk` /
   `addressLenOk` for the LoginStart/Handshake bounds so the caps live in one
   place.
6. Config knobs (not added, `config.h` is lead-owned): `max_packets_per_sec`,
   `max_bytes_per_sec`, `packet_burst`, `byte_burst` mapped into `LimiterConfig`.

Build/verify:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isrc \
    tests/test_limits.cpp src/security/limits.cpp -o build/test_limits && ./build/test_limits
```

`make test` already builds and runs it — the lead added `build/test_limits` to
`TEST_BINS`, the `build/test_limits:` rule and the `./build/test_limits` line in
the `test:` target. Verified green: `test_limits ok`. I made no `Makefile` edits.

The Python harnesses are intentionally **not** in `make test`: they need a live
server and the fuzzer sends deliberately hostile traffic. They are `make test-all`
/ manual material.

---

## 3. Integration matrix

Two files, split by ownership:

- `tests/integration/matrix.py` (lead) — happy path: status + login across 22 PVNs.
- `tests/integration/hardening_matrix.py` (D) — the rejection paths:
  bad-secret Velocity, oversize frame, 6-byte VarInt overflow, 1 MiB string field.

```
# server
./build/limbo --config config/limbo.properties

# happy path (lead)
python3 tests/integration/matrix.py 127.0.0.1 25566

# rejection paths (D), offline / forwarding=NONE
python3 tests/integration/hardening_matrix.py --host 127.0.0.1 --port 25566

# rejection paths against Velocity MODERN (forged HMAC + forwarded happy path)
python3 tests/integration/hardening_matrix.py --port 25599 \
    --secret wrong-secret --real-secret correct-horse-battery-staple

# single version
python3 tests/integration/hardening_matrix.py --pvns 767
```

Exit code 0 = every executed check passed; 1 = at least one FAIL. Per PVN the
hardening matrix runs 5 checks: status ping (`0x00` + JSON + ping echo), login
reaching `LoginSuccess 0x02` then the version-correct next state, 6-byte VarInt
overflow -> disconnect, oversize frame (`len > max_packet_bytes + 5`) ->
disconnect, and a 1 MiB username field refused (Login Disconnect, never a
successful login). With `--secret` it adds a forged Velocity HMAC ->
`Login Disconnect 0x00`. A `--secret`-less run against a MODERN server reports
login/bad-secret as `SKIP`, not `FAIL`.

Two next-state outcomes are accepted, both PASS:

- **v0.1 endpoint** — `LoginSuccess 0x02` then an immediate disconnect, no world.
  `pvn < 764`: Play Disconnect (`0x40` @47, `0x1A` @340/762/763, `0x19` @754).
  `pvn >= 764`: after `LoginAcknowledged 0x03` a Config Disconnect (`0x01` @764,
  `0x02` @766/767).
- **play burst (current trunk, A+B+C merged)** — `0x02` then Play is entered:
  JoinGame directly for `pvn < 764` (`0x01` @47, `0x23` @340, `0x24` @754,
  `0x28` @762/763), or `ack 0x03 -> RegistryData -> Finish -> ack -> JoinGame`
  for `pvn >= 764` (`0x29` @764, `0x2B` @766/767).

The check fails only when the chain is not version-correct (unknown id, EOF
before a decisive packet, wrong Finish ack). The matrices below were re-run
after the trunk merge, so the rows show the play-burst path; the v0.1 rows are
kept from the pre-merge run for reference.

### Results table (fill per run)

| date | endpoint | forwarding | PVN | status ping | login 0x02 | next state | varint overflow | oversize | bad secret |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-10-05 | v0.1 (pre-merge) | NONE | 47 | PASS | PASS | Play Disconnect 0x40 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | v0.1 (pre-merge) | NONE | 340 | PASS | PASS | Play Disconnect 0x1A | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | v0.1 (pre-merge) | NONE | 754 | PASS | PASS | Play Disconnect 0x19 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | v0.1 (pre-merge) | NONE | 762 | PASS | PASS | Play Disconnect 0x1A | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | v0.1 (pre-merge) | NONE | 763 | PASS | PASS | Play Disconnect 0x1A | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | v0.1 (pre-merge) | NONE | 764 | PASS | PASS | ack 0x03 -> Config Disconnect 0x01 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | v0.1 (pre-merge) | NONE | 766 | PASS | PASS | ack 0x03 -> Config Disconnect 0x02 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | v0.1 (pre-merge) | NONE | 767 | PASS | PASS | ack 0x03 -> Config Disconnect 0x02 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | trunk (A+B+C merged) | NONE | 47 | PASS | PASS | Play JoinGame 0x01 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | trunk (A+B+C merged) | NONE | 340 | PASS | PASS | Play JoinGame 0x23 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | trunk (A+B+C merged) | NONE | 754 | PASS | PASS | Play JoinGame 0x24 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | trunk (A+B+C merged) | NONE | 762 | PASS | PASS | Play JoinGame 0x28 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | trunk (A+B+C merged) | NONE | 763 | PASS | PASS | Play JoinGame 0x28 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | trunk (A+B+C merged) | NONE | 764 | PASS | PASS | ack -> RegistryData -> Finish 0x02 -> ack -> JoinGame 0x29 | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | trunk (A+B+C merged) | NONE | 766 | PASS | PASS | ack -> RegistryData -> Finish 0x03 -> ack -> JoinGame 0x2B | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | trunk (A+B+C merged) | NONE | 767 | PASS | PASS | ack -> RegistryData -> Finish 0x03 -> ack -> JoinGame 0x2B | PASS (reset) | PASS (reset) | SKIP |
| 2026-10-05 | trunk | MODERN | 763 | PASS | PASS (forwarded) | Play JoinGame 0x28 | PASS (reset) | PASS (reset) | PASS (Login Disconnect 0x00) |
| 2026-10-05 | trunk | MODERN | 767 | PASS | PASS (forwarded) | ack -> RegistryData -> Finish 0x03 -> ack -> JoinGame 0x2B | PASS (reset) | PASS (reset) | PASS (Login Disconnect 0x00) |

Totals on the current trunk: **40/40 PASS offline** (8 PVNs x 5 checks) and
**12/12 PASS with Velocity MODERN** (763+767 x 6 checks). The lead's
`matrix.py` separately reports `MATRIX PASS` over 22 PVNs.
`post-matrix liveness` (a fresh status ping at the end of every run) passed
every time.

---

## 4. Fuzz corpus

```
./build/limbo --config config/limbo.properties
python3 tests/fuzz/fuzz_suite.py --host 127.0.0.1 --port 25566
python3 tests/fuzz/fuzz_suite.py --port 25599 --rounds 3           # soak
python3 tests/fuzz/fuzz_suite.py --corpus tests/fuzz/corpus        # capture
python3 tests/fuzz/fuzz_suite.py --replay tests/fuzz/corpus/*.bin  # replay
python3 tests/fuzz/fuzz_suite.py --only varint --strict            # one case
```

The lead also keeps a faster smoke fuzzer at `tests/fuzz/fuzz_packet.py`; the
suite below is the superset.

Every case opens a fresh connection and the harness asserts two things:

1. the server terminates the connection (`disconnected` = silent close/reset,
   `rejected` = polite Disconnect then close) rather than hanging or replying,
2. a **clean status ping immediately afterwards still succeeds** — that is the
   "did not crash / did not wedge the accept loop" assertion. A failed recovery
   is reported as `FAIL <case>: recovery failed`.

`hang` is only acceptable for the four payloads that legitimately wait for more
bytes (`slowloris`, `half_open`, `len_mismatch_short`, `wrong_state_packets`),
because `read_timeout_ms=30000` exceeds the per-case socket timeout; those are
reported as notes, or as failures under `--strict` with `--timeout > 31`.

| case | what it attacks | expected server reaction |
| --- | --- | --- |
| `varint_overflow_len` | 6 continuation bytes in the frame length | close |
| `varint_overflow_len_after_valid` | a valid frame, then a 6-byte VarInt | close |
| `varint_overflow_id` | well-formed frame, non-terminating id VarInt | close |
| `len_mismatch_short` | declares 64 bytes, sends 3 | held until read timeout |
| `len_mismatch_overlong` | declares 2 bytes, sends 4 KiB | close |
| `zero_length` | `len == 0` (no room for a packet id) | close |
| `negative_length` | `VarInt(-1)` length | close |
| `int32min_length` | `VarInt(INT32_MIN)` length | close |
| `huge_string_username` | 1 MiB username prefix, 1 KiB payload | close, no allocation |
| `huge_string_username_full` | a real 1 MiB frame | close (`max_packet_bytes`) |
| `huge_string_address` | 1 MiB address in Handshake | close |
| `negative_string_len_username` | `VarInt(-1)` string length | close |
| `truncated_login_start` | username prefix cut mid-string | close |
| `empty_username` | zero-length username | Login Disconnect, close |
| `oversize_username_17` | 17-byte username | Login Disconnect, close |
| `oversize_packet` | single 1 MiB frame | close |
| `oversize_plugin_response` | `LoginPluginResponse` declaring 1 MiB | close |
| `bad_hmac` | forged (1-bit-flipped) Velocity signature | Login Disconnect |
| `bad_hmac_truncated` | signature cut mid-HMAC | Login Disconnect |
| `bad_hmac_garbage_props` | bogus version + 65535 properties | Login Disconnect |
| `plugin_response_wrong_msgid` | msgId 4242 instead of 0 | Login Disconnect |
| `plugin_response_negative_msgid` | msgId -1 | Login Disconnect |
| `plugin_response_failure_flag` | honest `success=false` | Login Disconnect |
| `wrong_state_packets` | Login packet where a status ping is expected | status reply, then read timeout |
| `login_disconnect_as_request` | client sends Login Disconnect to the server | close |
| `null_bytes` | 512 zero bytes | close |
| `random_garbage` | seeded random blob | close |
| `random_frames` | 32 frames with random/wrong declared lengths | close |
| `slowloris` | one byte, then silence | held until read timeout |
| `flood` | 5000 tiny frames in one burst | close |
| `half_open` | connect, send nothing | held until read timeout |

Observed on the trunk (2026-10-05): **31 cases, 0 failures, 3 rounds, server
alive afterwards**, against both `forwarding=NONE` and `forwarding=MODERN`.
Replaying the 31 captured `tests/fuzz/corpus/*.bin` files also passes with a
live status ping at the end.

Note on `flood`: it closes today because the malformed frames in the burst break
the state machine. The limiter is now wired into `readPacket`, but `flood` sends
5000 status requests against one connection — the first is a valid status
request, the rest arrive as a login packet where a ping is expected, so the
state machine still drops it before the rate budget is exhausted. A flood of
*valid* frames in a single state is the case the limiter exists for, and that
is covered by `testSustainedFloodRejected` (500 accepted / 9500 refused) rather
than by this wire payload.

---

## 5. Build/verify record

```
$ g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isrc \
      tests/test_limits.cpp src/security/limits.cpp -o build/test_limits
$ ./build/test_limits
test_limits ok

$ python3 -m py_compile tests/integration/hardening_matrix.py \
                       tests/fuzz/fuzz_suite.py
(clean)

$ python3 tests/integration/matrix.py 127.0.0.1 25566      # lead's happy path
pvn=47 OK status+login firstPlay=0x01
... (22 PVNs)
MATRIX PASS

$ python3 tests/integration/hardening_matrix.py --host 127.0.0.1 --port 25566
hardening matrix: 40 passed, 0 skipped, 0 failed
hardening matrix ok

$ python3 tests/integration/hardening_matrix.py --port 25599 \
      --secret wrong-secret --real-secret correct-horse-battery-staple \
      --pvns 763 767
hardening matrix: 12 passed, 0 skipped, 0 failed
hardening matrix ok

$ python3 tests/fuzz/fuzz_suite.py --host 127.0.0.1 --port 25566 --timeout 3
fuzz_suite ok: no crash, no hang, all hostile input handled

$ python3 tests/fuzz/fuzz_suite.py --host 127.0.0.1 --port 25566 \
      --rounds 3 --corpus tests/fuzz/corpus
fuzz_suite ok: no crash, no hang, all hostile input handled

$ python3 tests/fuzz/fuzz_suite.py --host 127.0.0.1 --port 25566 \
      --replay tests/fuzz/corpus/*.bin
fuzz replay: ok
```

All 8 unit binaries and the Python HMAC vector pass; the `Makefile` was not
edited by this workstream.