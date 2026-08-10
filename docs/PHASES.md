# MyPaaS statd — Development Phases

This document defines the implementation order and exit criteria for `mypaas-statd`.

The phases are intentionally incremental. Do not pull work from a later phase into the current phase merely because it appears useful or technically interesting. Finish the current contract, validate it, then advance.

## Testing and phase-gate policy

Every implementation phase must ship its tests with the production change. Testing is part of the phase, not follow-up cleanup.

Rules:
- each phase adds a dedicated `make test-phaseN` target;
- `make test` aggregates the smoke test plus every completed phase target;
- tests for a phase cover documented success, boundary, malformed-input, and failure cases before completion;
- deterministic tests are preferred; live-host tests are supplementary;
- every completed phase remains covered by later CI runs;
- GitHub Actions runs GCC and Clang tests, ASan/UBSan, and static analysis;
- a phase is complete only after the corresponding remote CI run is green;
- later bugs receive the smallest regression test at the phase/layer where they should have been caught.

Registry publishing and release packaging remain deferred until the implementation phases are complete and v0.1 behavior is validated.

## Phase 0 — Foundation and engineering contracts

**Goal:** establish a safe, boring, reproducible C project before kernel-facing implementation begins.

**Status:** complete.

## Phase 1 — Pure cgroup v2 parsers

**Goal:** correctly parse all v0.1 kernel text formats without requiring a live cgroup filesystem.

**Status:** complete. GCC, Clang, ASan/UBSan, and clang-tidy gates passed for the Phase 1 implementation.

## Phase 2 — cgroup reader and monotonic sampler

**Goal:** turn validated parsers into a small, correct sampler for explicitly registered cgroup paths.

Implemented scope:
- safe relative path traversal beneath configured cgroup root;
- fixed 4096-byte controller reads;
- `CLOCK_MONOTONIC` sampling state;
- CPU delta/reset handling;
- memory/PID snapshots and unlimited limits;
- latest-good snapshot storage plus last sample status;
- fixed registry bounds.

**Status:** complete. GCC, Clang, ASan/UBSan, and clang-tidy gates passed for the Phase 2 implementation.

## Phase 3 — Unix socket protocol v1

**Goal:** expose latest snapshots safely to the local MyPaaS control plane.

Implemented scope awaiting CI gate:
- AF_UNIX + SOCK_STREAM pathname server;
- nonblocking fixed-slot `poll()` loop;
- hello/protocol negotiation;
- register, unregister, snapshot, and status operations;
- 8 KiB input and 4 KiB output bounds per client;
- partial reads/writes and multiple messages per stream;
- malformed/oversized/disconnected client isolation;
- socket mode `0600` and cleanup;
- daemon runtime loop with independent periodic sampling.

Constraints retained:
- no epoll because eight clients do not justify it;
- no threads;
- no HTTP/gRPC/protobuf/shared memory;
- snapshot requests never trigger a full metrics sweep.

Exit criteria:
- `make test-phase3` covers fragmentation, multiple messages, oversized input, malformed request, unsupported version, handshake requirement, disconnect isolation, socket permissions/cleanup, registration, and snapshot response;
- all previous phase tests remain green;
- sanitizer clean;
- GitHub Actions green.

**Status:** in progress until remote CI validates the implementation commit.

## Phase 4 — MyPaaS integration and baseline comparison

**Goal:** prove that statd is operationally useful before broadening its scope.

Scope:
- Go-side client in MyPaaS;
- registration lifecycle integration;
- fallback/error behavior;
- benchmark current Docker CLI metrics path versus optimized Go/runtime path versus statd where practical;
- operational systemd service/release packaging, but no registry publishing yet.

Exit criteria:
- end-to-end metrics appear correctly in MyPaaS;
- lifecycle does not leak registrations;
- restart/reconnect behavior is tested;
- integration tests cover Go ↔ statd contract and failures;
- benchmark records CPU, RSS, latency, syscall/process cost, and freshness;
- statd provides meaningful measured benefit or integration is reconsidered;
- GitHub Actions green for integration changes.

## Phase 5 — Mature v0.1 hardening

**Goal:** make the proven design boring to operate.

Scope only as evidence requires:
- repeated-failure suppression/rate-limited logs;
- resource/FD leak tests;
- long-running soak test;
- packaging checks;
- compatibility documentation;
- benchmark-supported small performance improvements.

Exit criteria:
- long-running test shows bounded memory/FD behavior;
- graceful shutdown/restart is reliable;
- documentation matches implementation;
- all completed phase test targets remain green;
- v0.1 release artifacts are reproducible.

## Post-v0.1 ideas — not commitments

Potential later work includes IO metrics, PSI, OOM notifications, health probing, event-driven lifecycle integration, or other kernel interfaces. Each requires a separate need, contract, and benchmark. Do not pre-build abstractions for hypothetical future modules.

## Phase discipline

For every task:
1. identify the active phase;
2. implement the smallest change needed for that phase;
3. write/update phase tests in the same change;
4. do not create abstractions solely for future phases;
5. satisfy current exit criteria;
6. require green CI before marking complete;
7. benchmark only performance claims;
8. prefer deleting unnecessary complexity over hypothetical reuse.
