# TODO — limbo-c++ to full finish

Scope: all Java versions (1.8.x→1.21.x+), both Velocity MODERN + direct, both void + .schem v2.
Source of truth: docs/research/limbo-references.md, docs/ARCHITECTURE.md.

## Done
- [x] Wire primitives (VarInt/String/UUID/framing) + unit tests
- [x] Status ping + offline login + Velocity MODERN verify (accept/reject)
- [x] Dual-stack IPv6 (bind ::/0.0.0.0/hostname, v4-mapped normalize, %zone strip) + ::1 test
- [x] B/config-registry merged (mimo): NBT writer + RegistryData/Finish per era, live 764/767 verified
- [x] A/play-void-keepalive merged (longcat): JoinGame/Abilities/Position/GameEvent13/Center/void-chunk/KeepAlive loop, live 763+767 verified (burst IDs exact, echo ok)
- [x] Research notes + ARCHITECTURE hypotheses

## In progress (multi-agent, different models)
- [ ] A/play-void-keepalive (opencode/longcat-2.5-preview-free, ses_ef5e4b681ffe1HopAT3axM49aJ): Play burst per era — JoinGame, Abilities, PositionAndLook,
      GameEvent13 (≥1.20.3), void chunk(s), KeepAlive loop, movement-ignore, clean timeout/kick.
      Owns: src/world/void_chunk.*, src/protocol/play.* (new), tests/test_void_chunk.*.
      Must NOT edit src/server/connection.cpp (lead merges).
- [ ] B/config-registry (opencode/mimo-v2.6-flash-free, ses_ef5e4b67effehY9buwcMaznVlM): Configuration RegistryData + Finish for 764+ (dimension_type/biome/
      chat_type minimal NBT), KnownPacks passthrough, Finish ack. Owns: src/protocol/registry.*,
      src/protocol/nbt.* (new), tests/test_registry.*. Must NOT edit connection.cpp.
- [ ] C/schematic (opencode/nemotron-3.5-lightning-free, ses_ef5e4b66fffeEyQh6npW7G1YDS): Sponge .schem v2 reader
      with size caps + void fallback. Vendor miniz single-file (no sudo). Owns: src/world/schematic.*,
      third_party/miniz.*, tests/test_schematic.*. Must NOT edit connection.cpp.
- [ ] D/hardening-tests (opencode/space-bunny-free, ses_ef5e4b66cffeJ4KT5F1jowqFP4): rate limits (packets/sec, bytes/sec), max_players, read timeout,
      malformed-packet fuzz harness, integration matrix extend (47/340/754/762/763/764/766/767 + bad-secret +
      oversize + flood). Owns: tests/fuzz/*, tests/integration/matrix.py, src/security/limits.* (new).
      Must NOT edit connection.cpp.

## Lead merge queue (after agents return)
- [ ] Merge A+B+C+D into connection.cpp play/config flow (era-gated, version tables only)
- [ ] Full config surface (dimension/spawn/gamemode/motd/tablist/bossbar/join message) + example properties
- [ ] make test-all green: unit + HMAC + status/login + v6 + matrix + fuzz (no crash) + live 763/767 join-stay
- [ ] Soak (1k idle / join latency notes) + README final + ARCHITECTURE change log
- [ ] Acceptance: clean build, both direct+Velocity, void + .schem, all-eras handshake at least

## Boss checkpoint 2026-10-05 09:01 UTC
- Crew active: 4 background sessions, no completion notifications yet. Only partial landing:
  third_party/miniz.h (stub header from C). Build still green (make test ok).
- Rule enforced: crew owns new files only; connection.cpp/tcp_server.cpp merge is lead-only
  to avoid conflicts. No duplicate work started.
- Next: on each crew return → verify build+tests for its files → merge → update TODO boxes →
  run matrix/fuzz/soak → final acceptance.

## Boss verification 2026-10-05 09:12 UTC
- Trunk: GREEN after lead action (Makefile excludes broken schematic.cpp; added -I.).
- A/play (longcat, ses_ef5e4b68...): NO OUTPUT — missing void_chunk/play/tests/docs. Status ping sent, awaiting reply. NUDGE.
- B/registry (mimo, ses_ef5e4b67...): NO OUTPUT — missing nbt/registry/tests/docs. NUDGE.
- C/schematic (nemotron, ses_ef5e4b66...): BROKE TRUNK — schematic.cpp has 6+ compile errors
  (missing <array>, miniz gunzip signature mismatch, push_back() no-arg, rvalue size_t& pos at :475,
  excess initializers in miniz.h:311). Isolated via Makefile SRC filter. FIX REQUIRED, docs missing.
- D/hardening (space-bunny, ses_ef5e4b66...): PARTIAL PASS — limits.h/.cpp compile, test_limits ok;
  missing fuzz/*, matrix.py, docs. COMPLETE REMAINDER.
- Lead actions: Makefile -I. + SRC exclusion (lead-owned, no crew files touched).
- Help dispatched 09:15 UTC: A starter skeletons+build cmds, B NBT/registry starter, C 5 exact compile fixes,
  D matrix/fuzz skeletons. All via background sessions, no trunk edits by lead.

## Acceptance gate
- Vanilla client join/stay/kick per era via direct + Velocity MODERN; wrong secret rejected;
  malformed/flood causes disconnect not crash; void fallback always works; no sudo/cmake needed.
