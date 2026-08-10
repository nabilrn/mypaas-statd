# MyPaaS statd — Development Phases

This document defines the implementation order and exit criteria for `mypaas-statd`.

The phases are intentionally incremental. Do not pull work from a later phase into the current phase merely because it appears useful or technically interesting. Finish the current contract, validate it, then advance.

## Phase 0 — Foundation and engineering contracts

**Goal:** establish a safe, boring, reproducible C project before kernel-facing implementation begins.

Scope:
- C17 Linux-only project skeleton;
- compiler warnings, sanitizers, static analysis, formatting, CI;
- `AGENTS.md` and project-specific skills;
- architecture, cgroup, IPC, and benchmarking contracts;
- smoke build/test only; no real sampling logic.

Exit criteria:
- GCC and Clang builds are warning-clean under project flags;
- smoke tests pass;
- sanitizer target passes;
- CI is present;
- implementation boundaries and v0.1 exclusions are documented.

**Status:** complete.

## Phase 1 — Pure cgroup v2 parsers

**Goal:** correctly parse all v0.1 kernel text formats without requiring a live cgroup filesystem.

Scope:
- parse `cpu.stat`;
- parse `cpu.max` including `max`;
- parse `memory.current`;
- parse `memory.max` including `max`;
- parse `memory.events`;
- parse `pids.current`;
- parse `pids.max` including `max`;
- deterministic fixtures and unit tests.

Constraints:
- parsers operate on explicit byte/string input and are independent from file I/O;
- do not calculate CPU percentage yet;
- do not add sockets, threads, epoll, timers, or registration state;
- unknown documented future keys may be ignored only where contract-safe;
- malformed required values must fail explicitly.

Exit criteria:
- fixtures cover valid, malformed, missing-field, overflow/range, whitespace, and `max` cases;
- parser tests pass under GCC and Clang;
- ASan/UBSan clean;
- no live-host dependency in parser tests.

## Phase 2 — cgroup reader and monotonic sampler

**Goal:** turn validated parsers into a small, correct sampler for explicitly registered cgroup paths.

Scope:
- safe path validation beneath configured cgroup root;
- bounded file reads;
- `CLOCK_MONOTONIC` timestamps;
- previous/current CPU counter state;
- CPU usage delta calculation;
- memory/PID snapshots;
- latest snapshot storage;
- registration state with fixed/documented bounds.

Constraints:
- MyPaaS supplies the resolved cgroup path; statd does not infer Docker layout;
- no IPC server yet beyond test harnesses if useful;
- prefer simple open/read/close behavior initially; persistent FDs are an optimization only if measurements justify them;
- no threads unless correctness cannot be achieved simply without them.

Exit criteria:
- deterministic sampler-state tests cover first sample, subsequent sample, counter regression/reset, zero/short elapsed interval, disappearing cgroup, and unlimited limits;
- live Linux integration test can sample a controlled cgroup when the environment permits;
- sanitizer clean.

## Phase 3 — Unix socket protocol v1

**Goal:** expose latest snapshots safely to the local MyPaaS control plane.

Scope:
- AF_UNIX + SOCK_STREAM server at configured path;
- protocol handshake/versioning;
- register, unregister, snapshot, and basic status operations as documented in `IPC_PROTOCOL.md`;
- bounded clients and messages;
- partial read/write handling;
- clean disconnect and malformed-request handling.

Constraints:
- start with the simplest correct connection model;
- do not add epoll unless the simple model demonstrably fails the expected client workload or would require an undesirable thread-per-client model;
- no HTTP/gRPC/protobuf/shared memory.

Exit criteria:
- protocol integration tests cover fragmentation, multiple messages, oversized input, malformed JSON, unsupported version/op, disconnects, and slow/broken clients;
- daemon remains alive after client protocol errors;
- socket permissions and cleanup behavior are tested.

## Phase 4 — MyPaaS integration and baseline comparison

**Goal:** prove that statd is operationally useful before broadening its scope.

Scope:
- Go-side client in MyPaaS;
- registration lifecycle integration;
- fallback/error behavior defined explicitly;
- benchmark current Docker CLI metrics path versus optimized Go/runtime path versus statd where practical;
- operational systemd service/release packaging.

Exit criteria:
- end-to-end metrics appear correctly in MyPaaS;
- container/project lifecycle does not leak registrations;
- daemon restart/reconnect behavior is defined and tested;
- benchmark report records CPU, RSS, latency, syscall/process cost, and metric freshness;
- statd provides a meaningful measured benefit or the integration is reconsidered.

## Phase 5 — Mature v0.1 hardening

**Goal:** make the proven design boring to operate.

Scope only as evidence requires:
- repeated-failure suppression/rate-limited logs;
- resource/FD leak tests;
- long-running soak test;
- packaging checks;
- compatibility documentation;
- small performance improvements supported by benchmark evidence.

Exit criteria:
- long-running test shows bounded memory/FD behavior;
- graceful shutdown/restart is reliable;
- documentation matches implementation;
- v0.1 release artifacts are reproducible.

## Post-v0.1 ideas — not commitments

Potential later work includes IO metrics, PSI, OOM notifications, health probing, event-driven lifecycle integration, or other kernel interfaces. Each requires a separate need, contract, and benchmark. Do not pre-build abstractions for hypothetical future modules.

## Phase discipline

For every task:
1. identify the active phase;
2. implement the smallest change needed for that phase;
3. do not create abstractions solely for future phases;
4. satisfy the current exit criteria;
5. benchmark only claims that are performance-related;
6. prefer deleting unnecessary complexity over preserving it for hypothetical reuse.
