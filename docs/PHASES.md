# MyPaaS statd — Development Phases

Development is incremental: implementation, tests, sanitizers, static analysis, then remote CI gate before advancing.

## Testing and phase-gate policy

- every phase adds/updates its test target;
- `make test` retains all completed phase regression tests;
- GitHub Actions validates GCC, Clang, ASan/UBSan, and clang-tidy;
- registry publishing stays deferred until v0.1 is hardened.

## Phase 0 — Foundation
**Status:** complete.

## Phase 1 — Pure cgroup v2 parsers
**Status:** complete; remote gates green.

## Phase 2 — cgroup reader and monotonic sampler
**Status:** complete; remote gates green.

## Phase 3 — Unix socket protocol v1
**Status:** complete; GCC, Clang, ASan/UBSan, and clang-tidy gates passed.

Implemented: fixed-slot nonblocking `poll()` server, hello, register/unregister/snapshot/status, bounded stream handling, socket permissions/cleanup, and independent periodic sampling.

## Phase 4 — MyPaaS integration and baseline comparison

**Status:** in progress.

Integration boundary established:
- MyPaaS obtains a container host PID once during runtime lifecycle transitions;
- production IPC register sends that PID to statd;
- statd resolves cgroup v2 membership from `/proc/<pid>/cgroup` using the documented `0::$PATH` entry;
- statd still never talks to Docker or guesses Docker/systemd cgroup layout;
- direct relative-cgroup registration remains available only for deterministic tests/admin diagnostics.

Remaining Phase 4 scope:
- tested Go protocol client on an isolated MyPaaS integration slice;
- Docker lifecycle PID discovery helpers;
- register/unregister/reconcile integration;
- metrics read path with explicit Docker fallback;
- end-to-end tests;
- benchmark baseline and systemd packaging.

## Phase 5 — Mature v0.1 hardening

Pending until Phase 4 proves operational value. Registry/release publishing remains deferred.

## Phase discipline

Prefer the smallest mature implementation that satisfies the current phase. Do not build future mechanisms early.
