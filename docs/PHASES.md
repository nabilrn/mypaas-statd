# MyPaaS statd — Development Phases

Development is incremental: implementation, tests, sanitizers, static analysis, then remote CI gate before advancing.

## Testing and phase-gate policy

- every phase adds/updates its test target;
- `make test` retains all completed phase regression tests;
- GitHub Actions validates GCC, Clang, ASan/UBSan, and clang-tidy;
- deterministic correctness checks run in CI; production performance claims require the target host;
- registry publishing stays deferred until v0.1 is hardened.

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

**Status:** implementation complete; validation blocked only on real-host benchmark evidence.

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

Remaining Phase 4 gate:
1. install/run statd on the real MyPaaS VM;
2. execute `benchmarks/compare.py` under representative workloads;
3. record repeated latency/CPU/RSS/freshness observations;
4. confirm that the measured benefit justifies the additional host daemon;
5. only then mark Phase 4 complete and merge/enable the MyPaaS integration.

The benchmark harness itself is not evidence. GitHub-hosted CI performance is not accepted as a substitute for the target VM.

## Phase 5 — Mature v0.1 hardening

**Status:** blocked by Phase 4 benchmark gate.

Do not start Phase 5 merely because the implementation compiles. Once Phase 4 is accepted, Phase 5 may add only evidence-driven hardening such as:
- bounded long-running/FD behavior checks;
- graceful restart/soak validation;
- packaging compatibility checks;
- measured small optimizations if needed;
- final release artifact workflow.

Registry/GitHub Release publishing remains deferred until this phase is complete.

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
