# AGENTS.md — mypaas-statd

Persistent engineering contract for AI agents and contributors. Keep this file concise, enforceable, and synchronized with `docs/`.

## Mission

`mypaas-statd` is a Linux-native telemetry daemon used by MyPaaS. It exists to remove repeated process creation and Docker CLI work from the runtime metrics hot path. It samples Linux cgroup v2 counters directly and exposes latest snapshots to the MyPaaS Go control plane through a local Unix domain socket.

MyPaaS remains the source of runtime identity. For registration it supplies a stable runtime ID plus exactly one runtime locator:
- a cgroup path relative to statd's configured cgroup root; or
- a positive host PID obtained from the container runtime. statd resolves that PID through host `/proc/<pid>/cgroup` and then applies the same cgroup-path validation.

This project is not a general monitoring agent and not a container runtime.

## Phase discipline

Read `docs/PHASES.md` before implementation work.

- Identify the active phase before coding.
- Implement only what is required by the current phase and its exit criteria.
- Write/update phase tests in the same change.
- Do not pull later-phase mechanisms into the current phase for speculative reuse.
- Do not create abstractions for features that do not exist yet.
- Complete correctness, tests, and quality gates for the current phase before expanding scope.
- A phase is complete only after its remote GitHub Actions run is green.

## Simplicity and maturity rule

The preferred implementation is the simplest mature design that satisfies the documented contract correctly and measurably.

"Low-level" does not mean "maximum complexity". This project values boring, explicit, maintainable systems code.

Prefer:
- standard Linux/POSIX interfaces with stable semantics;
- small modules with one clear responsibility;
- direct control flow over framework-like abstraction;
- plain structs and explicit functions over generic object systems;
- fixed/documented bounds where the workload is naturally bounded;
- straightforward parsing and state machines that are easy to test;
- code a competent C/Linux engineer can understand without reconstructing hidden machinery.

Avoid unless current requirements and measurements justify them:
- generic containers/frameworks built inside the project;
- macro metaprogramming beyond small compile-time helpers;
- callback layers that obscure ownership or control flow;
- premature plugin architectures;
- unnecessary indirection or generic dispatch;
- clever bit tricks when ordinary arithmetic is clear enough;
- custom allocators, lock-free structures, shared memory, io_uring, eBPF, SIMD/manual assembly, thread pools;
- caches without a demonstrated repeated cost.

A little duplication is preferable to a premature abstraction when the abstraction makes ownership, error handling, or kernel semantics harder to understand. Do not optimize for minimum line count; optimize for correctness, bounded behavior, readability, and measured runtime cost.

## Locked decisions

- Language: C17.
- Platform: Linux only.
- Runtime source of truth: cgroup v2 and explicitly documented Linux kernel interfaces.
- Host PID resolution source: configured procfs root, default `/proc`.
- IPC v0.1: AF_UNIX + SOCK_STREAM.
- Protocol v0.1: bounded newline-delimited UTF-8 JSON subset.
- Daemon model: foreground process; service supervision belongs to systemd.
- Sampling model: persistent periodic sampler; requests read the latest snapshot instead of triggering expensive collection work.
- Prefer a single-threaded design until measurements prove threads are required.
- Build system: Make for v0.1. Do not introduce CMake/Meson without a demonstrated need.

## v0.1 implementation scope

Implement only:
- registration/unregistration with a stable runtime ID and exactly one locator: relative cgroup path or host PID;
- host PID → cgroup v2 path resolution from `/proc/<pid>/cgroup`;
- CPU usage sampling from `cpu.stat`;
- CPU quota metadata from `cpu.max`;
- memory usage/limit/events from `memory.current`, `memory.max`, `memory.events`;
- PID usage/limit from `pids.current`, `pids.max`;
- monotonic-time delta calculation;
- latest in-memory snapshots;
- Unix socket request/response protocol;
- bounded clients, message sizes, registrations, paths, and buffers;
- clean SIGTERM/SIGINT shutdown.

Do not implement in v0.1:
- Docker Engine client or Docker CLI execution in statd;
- inference of Docker/systemd cgroup directory naming;
- eBPF, io_uring, shared memory, custom allocators, lock-free queues;
- plugin systems, HTTP/gRPC, process/container management, log streaming, health probes, automatic remediation.

## Non-negotiable anti-hallucination rules

Kernel-facing behavior MUST NOT be implemented from memory alone.

Before changing behavior that depends on cgroup v2, procfs, sysfs, Unix sockets, `poll`/`epoll`, timerfd, pidfd, signals, filesystem lifetime, namespaces, or kernel limits:
1. identify the exact Linux interface/file/syscall;
2. verify semantics using authoritative Linux documentation/man-pages available to the environment;
3. state project-specific assumptions in code comments or docs;
4. add/update deterministic fixtures for external text formats;
5. test parsing, missing fields, malformed input, limits, and special values;
6. prefer an explicit unsupported/error result over invented portability or fallback behavior.

Never hardcode Docker's cgroup filesystem layout. A host PID may be used only as a lookup key into the documented procfs cgroup membership interface. cgroup paths are still validated beneath the configured cgroup root.

## No shelling out

The daemon must never use `system()`, `popen()`, shell scripts, or `exec*()` to collect/resolve runtime metrics. Never invoke `docker`, `cat`, `grep`, `awk`, or similar commands from runtime code. Shell use in developer tooling/CI is fine.

## C safety rules

- No unchecked allocation; every allocation has obvious ownership and deterministic destruction.
- Guard size arithmetic before allocation/copy.
- Never assume `read()`/`recv()` data is NUL-terminated; track explicit lengths.
- Prefer `snprintf`; never use unsafe unbounded string functions.
- Check relevant syscall return values and capture `errno` before later calls overwrite it.
- Every successful FD acquisition needs a deterministic close path.
- Prefer CLOEXEC variants at creation.
- Handle partial stream reads/writes.
- Do not use undefined behavior as an optimization technique.

## Performance rules

Performance is measured, not assumed.

Optimization order:
1. remove unnecessary work;
2. avoid process creation and redundant parsing;
3. reuse persistent descriptors/connections when measured behavior benefits;
4. bound allocations and copies;
5. use `poll`/`epoll` only when the connection model needs it;
6. consider advanced mechanisms only after benchmark evidence.

When two implementations meet requirements and their measured difference is operationally insignificant, choose the simpler implementation. No performance claim may be published without reproducible benchmark evidence.

## Security boundary

The Unix socket is a privileged local control-plane interface.
- Default socket: `/run/mypaas/statd.sock`; no TCP listener.
- Default cgroup root: `/sys/fs/cgroup`; default proc root: `/proc`.
- Treat runtime IDs, cgroup paths, and PIDs from the peer as untrusted inputs.
- Reject traversal/symlink escapes beneath configured roots.
- For host PID resolution, open the configured proc root and PID directory with no-follow semantics and read only the bounded `cgroup` file.
- Bound message size, client count, registrations, identifier lengths, paths, and output buffers.

## Build quality gates

Default warnings include at least `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Wundef`.

Before a change is complete, run the applicable gates:
```bash
make
make test
make sanitize
make lint
```

Every completed phase remains included in `make test` and sanitizer CI. Release optimization defaults to `-O2`; do not use `-Ofast` by default.

## Repository structure

```text
src/             production implementation
include/         project headers
tests/           unit/integration tests
fixtures/        deterministic external-interface fixtures
benchmarks/      benchmark programs/scripts and methodology
docs/            architecture and stable contracts
.agents/skills/  task-specific agent guidance
```

Keep modules narrow. Do not create generic utility dumping grounds.

## Documentation precedence

- `AGENTS.md`: engineering constitution and scope.
- `docs/PHASES.md`: implementation order and exit criteria.
- `docs/ARCHITECTURE.md`: component boundaries/data flow.
- `docs/CGROUP_V2.md`: metric and cgroup/proc resolution semantics.
- `docs/IPC_PROTOCOL.md`: wire contract.
- `docs/BENCHMARKING.md`: valid performance methodology.

If implementation and docs disagree, resolve the contract explicitly; do not silently choose one.
