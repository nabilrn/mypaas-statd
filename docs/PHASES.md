# MyPaaS statd — Development Phases

This document defines implementation order and exit criteria. Finish and verify each phase before expanding scope.

## Testing and phase-gate policy

Every implementation phase ships tests with production changes.
- each phase has `make test-phaseN`;
- `make test` aggregates smoke + all completed phases;
- tests cover success, bounds, malformed input, and failure behavior;
- deterministic tests are preferred; live-host tests are supplementary;
- every completed phase remains covered by later CI;
- GitHub Actions runs GCC, Clang, ASan/UBSan, and static analysis;
- a phase is complete only after corresponding remote CI is green;
- regressions add the smallest test at the layer that should have caught them.

Registry/release publishing is deferred until implementation phases are complete and v0.1 behavior is validated.

## Phase 0 — Foundation and engineering contracts

**Goal:** safe, reproducible C project and engineering guardrails.

**Status:** complete.

## Phase 1 — Pure cgroup v2 parsers

**Goal:** parse v0.1 controller text independently from file I/O.

**Status:** complete; GCC/Clang/ASan/UBSan/clang-tidy green.

## Phase 2 — cgroup reader and monotonic sampler

**Goal:** safely read registered cgroups and maintain cached snapshots.

Implemented: safe relative traversal, bounded controller reads, `CLOCK_MONOTONIC`, CPU deltas/reset, memory/PID snapshots, fixed registration bounds, latest-good snapshot + sample status.

**Status:** complete; all gates green.

## Phase 3 — Unix socket protocol v1

**Goal:** safely expose cached snapshots to local MyPaaS clients.

Implemented: AF_UNIX/SOCK_STREAM, fixed-slot `poll()` server, hello/versioning, register/unregister/snapshot/status, bounded partial I/O, client isolation, mode 0600, independent periodic sampling.

**Status:** complete; protocol integration tests and all quality gates green.

## Phase 4 — MyPaaS integration and baseline comparison

**Goal:** prove statd is operationally useful before broadening scope.

Completed Phase 4 slices:
- tested Go protocol client on an isolated MyPaaS branch;
- host PID → cgroup-v2 resolver in statd using configured procfs root;
- `register` accepts exactly one locator (`pid` or `cgroup`) while preserving direct-cgroup compatibility;
- resolver/protocol changes are covered by `test-phase4` plus Phase 3 IPC regressions and have passed GCC, Clang, sanitizer, and clang-tidy gates.

Remaining scope:
- finish/review MyPaaS runtime registration lifecycle and Docker fallback behavior;
- ensure Compose fast-path snapshots do not perform Docker discovery each refresh;
- define restart/reconnect/re-registration behavior;
- add end-to-end integration testing where practical;
- benchmark current Docker CLI path vs statd path for CPU/RSS/latency/process cost/freshness;
- add operational systemd/service installation and API socket mount only after integration behavior is stable;
- no registry publishing yet.

Exit criteria:
- end-to-end metrics are correct in MyPaaS;
- runtime lifecycle does not leak registrations;
- normal statd snapshot hot path avoids Docker CLI/process creation;
- daemon restart/reconnect behavior is tested;
- benchmark shows meaningful benefit or integration is reconsidered;
- GitHub Actions green for both repositories.

**Status:** in progress.

## Phase 5 — Mature v0.1 hardening

**Goal:** make the proven design boring to operate.

Evidence-driven scope: repeated-failure suppression, FD/resource leak checks, soak testing, packaging checks, compatibility docs, benchmark-supported small optimizations.

Exit criteria: bounded long-running memory/FD behavior, reliable graceful restart, documentation synchronized, all phase tests green, reproducible v0.1 artifacts.

## Post-v0.1 ideas — not commitments

I/O metrics, PSI, OOM notifications, health probing, event-driven lifecycle integration, or other kernel interfaces require separate need/contracts/benchmarks. Do not pre-build abstractions for hypothetical modules.

## Phase discipline

For every task: identify active phase → implement smallest needed change → write tests → satisfy exit criteria → require green CI → benchmark performance claims → remove unnecessary complexity.
