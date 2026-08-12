# MyPaaS statd — Development Phases

Development is incremental: implementation, tests, sanitizers, static analysis, then remote CI gate before advancing.

## Testing and phase-gate policy

- every phase adds/updates its test target;
- `make test` retains all completed phase regression tests;
- GitHub Actions validates GCC, Clang, ASan/UBSan, and clang-tidy;
- deterministic correctness checks run in CI; production performance claims require the target host;
- release publishing occurs only from a verified release source.

## Phase 0 — Foundation

**Status:** complete.

Established C17/Linux-only scope, build hygiene, architecture contracts, agent guidance, sanitizers, static analysis, and CI.

## Phase 1 — Pure cgroup v2 parsers

**Status:** complete; remote gates green.

Implemented allocation-free and I/O-free parsers for the v0.1 cgroup text formats with malformed, range, whitespace, missing, duplicate, unknown-key, and `max` cases covered by tests.

## Phase 2 — cgroup reader and monotonic sampler

**Status:** complete; remote gates green.

Implemented safe relative cgroup traversal, bounded controller reads, `CLOCK_MONOTONIC` CPU deltas, fixed registration state, last-good snapshots, and deterministic sampler tests.

## Phase 3 — Unix socket protocol v1

**Status:** complete; remote gates green.

Implemented:
- fixed-slot nonblocking `poll()` server;
- `hello`, `register`, `unregister`, `snapshot`, and `status`;
- bounded partial-read/write handling;
- protocol/error isolation;
- socket permission and cleanup checks;
- independent periodic sampling.

No `epoll`, threads, HTTP, gRPC, protobuf, shared memory, or generic framework was added because the current workload does not justify them.

## Phase 4 — MyPaaS integration and baseline comparison

**Status:** complete; real-host performance gate accepted and MyPaaS integration merged.

Completed statd-side work:
- production PID registration resolves cgroup v2 membership from `/proc/<pid>/cgroup` using the documented unified entry;
- statd does not talk to Docker and does not guess Docker/systemd cgroup layout;
- direct relative-cgroup registration remains only for deterministic tests/admin diagnostics;
- disappeared cgroups are automatically evicted from the fixed registry during periodic sampling, preventing stale registration/FD accumulation when the control plane disappears or misses cleanup;
- transient sampling failures preserve the last good snapshot instead of fabricating zeroes;
- systemd service packaging and staged `make install` verification are implemented;
- real-host Docker CLI versus statd latency benchmark harness is implemented and syntax-gated in CI;
- operations and rollback procedures are documented.

Completed MyPaaS integration work on `agent/statd-phase4-client` / PR #15:
- tested stdlib-only Go Unix-socket protocol client;
- production registration uses host PID;
- single-container runtime PID discovery uses one Docker inspect on registration/recovery paths;
- Compose runtime metadata uses one container discovery plus one batched Docker inspect rather than per-service inspect loops;
- REST metrics and SSE metrics prefer statd when `STATD_SOCKET` is configured;
- statd failures explicitly fall back to the existing Docker metrics path;
- production Compose shares only `/run/mypaas` with the API and does not mount host `/proc` or `/sys/fs/cgroup` into the API container;
- `STATD_SOCKET` defaults empty, so rollout is opt-in and reversible;
- backend, frontend, and production Compose CI checks are green for the integration branch.

Intentional lifecycle choice:
- MyPaaS does not need to wire unregister calls through every stop/delete/crash path;
- statd self-evicts only registrations whose required cgroup files are actually gone;
- this keeps lifecycle coupling small and recovers even if the Go control plane crashes.

Accepted Phase 4 evidence:
- real Debian 13 + Podman host benchmark evidence is preserved under `benchmarks/results/`;
- repeated trials compare Docker-compatible CLI metrics against statd protocol v1;
- correctness was validated against raw cgroup v2 files;
- statd stop/restart/disappearance behavior was exercised through the MyPaaS integration;
- MyPaaS PR #15 merged the opt-in statd client and fallback behavior.

The benchmark harness itself is not evidence. GitHub-hosted CI performance is not accepted as a substitute for the target VM.

## Phase 5 — Mature v0.1 hardening and release

**Status:** complete; v0.1.0 published.

Completed evidence-driven hardening includes:
- bounded long-running/FD behavior checks;
- graceful shutdown/restart validation;
- disappeared-cgroup eviction through the normal sampling loop;
- packaging compatibility checks;
- a versioned Linux amd64 release package with checksum verification;
- a tag-triggered verified GitHub Release workflow.

The published v0.1.0 source is the accepted release baseline for future compatibility work.

## Phase 6 — Host storage and network telemetry

**Status:** active.

Goal: add the smallest mature host-level telemetry contract needed by the MyPaaS workspace dashboard without turning statd into a general monitoring agent.

Phase 6 scope:
- root-filesystem total and available bytes using `statvfs(3)`;
- IPv4 default-route interface selection from bounded `/proc/net/route` rows;
- cumulative RX/TX byte counters from `/sys/class/net/<iface>/statistics/`;
- independent validity for storage and network sections;
- deterministic fixtures/tests for route selection, counters, malformed/unavailable sources, and bounds;
- additive protocol-v1 exposure in a later Phase 6 slice after the host readers are green;
- MyPaaS control-plane integration only after the statd wire contract is tested.

Explicitly out of scope for Phase 6:
- eBPF;
- packet capture;
- traffic control;
- netlink-based per-container accounting;
- Docker/Podman API calls from statd;
- per-project network usage;
- filesystem/volume scanning;
- time-series persistence;
- a general host monitoring agent.

The host-reader semantics are documented in `docs/HOST_TELEMETRY.md`.

## Phase discipline

For every task:
1. identify the active phase;
2. implement the smallest mature change needed for that phase;
3. add or update tests in the same change;
4. retain previous phase regression tests;
5. require green correctness CI;
6. require real target-host evidence for performance claims;
7. do not create abstractions for hypothetical later phases;
8. prefer deleting unnecessary complexity over preserving it for imagined reuse.
