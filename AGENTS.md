# AGENTS.md — mypaas-statd

Persistent engineering contract for AI agents and contributors. Keep this file concise, enforceable, and synchronized with `docs/`.

## Mission

`mypaas-statd` is a Linux-native telemetry daemon used by MyPaaS. It exists to remove repeated process creation and Docker CLI work from the runtime metrics hot path. It samples Linux cgroup v2 counters directly and exposes latest snapshots to the MyPaaS Go control plane through a local Unix domain socket.

This project is not a general monitoring agent and not a container runtime.

## Phase discipline

Read `docs/PHASES.md` before implementation work.

- Identify the active phase before coding.
- Implement only what is required by the current phase and its exit criteria.
- Do not pull later-phase mechanisms into the current phase for speculative reuse.
- Do not create abstractions for features that do not exist yet.
- Complete correctness, tests, and quality gates for the current phase before expanding scope.
- If a later-phase feature appears necessary to finish the current phase, document why before introducing it.

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
- code that a competent C/Linux engineer can understand without reconstructing hidden machinery.

Avoid unless current requirements and measurements justify them:
- generic containers/frameworks built inside the project;
- macro metaprogramming beyond small compile-time helpers;
- callback layers that obscure ownership or control flow;
- premature plugin architectures;
- unnecessary indirection, inheritance-like patterns, or generic dispatch;
- clever bit tricks when ordinary arithmetic is clear enough;
- custom memory allocators;
- lock-free structures;
- shared memory;
- io_uring;
- eBPF;
- SIMD/manual assembly;
- thread pools;
- caches without a demonstrated repeated cost.

A little duplication is preferable to a premature abstraction when the abstraction would make ownership, error handling, or kernel semantics harder to understand. Refactor only after a real repeated pattern is visible and the simpler API is clear.

Do not optimize for minimum line count. Optimize for correctness, bounded behavior, readability, and measured runtime cost.

## Locked decisions

- Language: C17.
- Platform: Linux only.
- Runtime source of truth: cgroup v2 and explicitly documented Linux kernel interfaces.
- IPC v0.1: AF_UNIX + SOCK_STREAM.
- Protocol v0.1: bounded newline-delimited UTF-8 JSON messages.
- Daemon model: foreground process; service supervision belongs to systemd.
- Sampling model: persistent periodic sampler; requests read the latest snapshot instead of triggering expensive collection work.
- Prefer a single-threaded design until measurements prove threads are required.
- Build system: Make for v0.1. Do not introduce CMake/Meson without a demonstrated need.

## v0.1 implementation scope

Implement only:
- cgroup v2 discovery contract supplied by the MyPaaS control plane;
- registration/unregistration of explicitly supplied cgroup paths;
- CPU usage sampling from `cpu.stat`;
- CPU quota metadata from `cpu.max` when needed by the metric contract;
- memory usage/limit/events from `memory.current`, `memory.max`, `memory.events`;
- PID usage/limit from `pids.current`, `pids.max`;
- monotonic-time delta calculation;
- latest in-memory snapshots;
- Unix socket request/response protocol;
- bounded clients, message sizes, registrations, and buffers;
- clean SIGTERM/SIGINT shutdown.

Do not implement in v0.1:
- eBPF;
- io_uring;
- shared memory;
- custom allocators;
- lock-free queues;
- plugin systems;
- HTTP/gRPC;
- Docker Engine client;
- Docker CLI execution;
- process/container management;
- log streaming;
- health probes;
- automatic restart/remediation.

## Non-negotiable anti-hallucination rules

Kernel-facing behavior MUST NOT be implemented from memory alone.

Before changing behavior that depends on cgroup v2, procfs, sysfs, Unix sockets, `epoll`, `timerfd`, `pidfd`, signals, filesystem lifetime, or kernel limits:
1. identify the exact Linux interface and file/syscall involved;
2. verify its documented semantics using authoritative Linux documentation/man-pages available to the environment;
3. state any project-specific assumptions in code comments or docs;
4. add or update fixtures for text formats;
5. add tests for parsing, missing fields, malformed input, limits, and special values such as `max`;
6. prefer returning an explicit unsupported/error result over inventing portability or fallback behavior.

Never hardcode Docker's cgroup filesystem layout. MyPaaS supplies an already-resolved cgroup path under the allowed cgroup root. statd validates and observes it; it does not guess `/sys/fs/cgroup/docker/...` paths.

## No shelling out

The daemon must never use `system()`, `popen()`, shell scripts, or `exec*()` to collect runtime metrics. Never invoke `docker stats`, `docker inspect`, `docker ps`, `cat`, `grep`, `awk`, or similar commands from the daemon.

Using a shell in developer tooling/CI is fine; using it in runtime collection code is not.

## C safety rules

- No unchecked `malloc`, `calloc`, or `realloc`.
- Every allocation has an obvious owner and deterministic destruction path.
- Guard size arithmetic before allocation/copy.
- Never assume `read()`/`recv()` data is NUL-terminated.
- Track explicit buffer lengths.
- Prefer `snprintf`; do not use `sprintf`, `strcpy`, `strcat`, `gets`, or unbounded `%s` parsing.
- Check all relevant syscall return values.
- Capture `errno` before a later call can overwrite it.
- Every successful `open`, `socket`, `accept`, `epoll_create1`, `timerfd_create`, or similar FD acquisition needs a deterministic close path.
- Set close-on-exec where appropriate (`O_CLOEXEC`, `SOCK_CLOEXEC`, `EPOLL_CLOEXEC`).
- Do not ignore partial writes/reads on stream sockets.
- Do not use undefined behavior as an optimization technique.

## Performance rules

Performance is measured, not assumed.

Optimization order:
1. remove unnecessary work;
2. avoid process creation and redundant parsing;
3. reuse persistent descriptors/connections when it improves measured behavior;
4. bound allocations and copies;
5. consider `poll`/`epoll` only when the connection model needs it;
6. consider advanced mechanisms only after benchmark evidence.

Do not introduce shared memory, io_uring, eBPF, custom allocators, SIMD, or lock-free structures merely because they sound low-latency.

When two implementations meet the requirement and their measured difference is operationally insignificant, choose the simpler implementation.

No README or code comment may claim a speedup versus Docker/Go without reproducible benchmark evidence.

## Error model

- Functions return explicit status values; use `0` for success where practical.
- Preserve actionable context at subsystem boundaries.
- Never silently swallow malformed kernel data.
- Distinguish: not found, unsupported, invalid input, transient I/O failure, protocol error, and internal failure where the caller benefits from the distinction.
- Runtime client errors must not crash the daemon.

## Security boundary

The Unix socket is a privileged local control-plane interface.
- Default socket path: `/run/mypaas/statd.sock`.
- Do not bind TCP ports.
- Do not follow arbitrary user-controlled paths outside the configured cgroup root.
- Validate registration paths canonically before storing/opening them.
- Reject path traversal and symlink escapes from the allowed root.
- Bound message size, client count, registrations, identifier lengths, and all dynamic buffers.
- Never trust JSON fields because the peer is local.

## Build quality gates

Default warnings include at least:
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Wundef`.

Debug/sanitizer validation uses AddressSanitizer + UndefinedBehaviorSanitizer and frame pointers.

Before considering a change complete, run what is applicable:
```bash
make
make test
make sanitize
make lint
```

Release optimization defaults to `-O2`. Do not use `-Ofast` by default.

## Repository structure

```text
src/             production implementation
include/         public/internal project headers
tests/           unit/integration tests
fixtures/        deterministic kernel-file fixtures
benchmarks/      benchmark programs/scripts and results methodology
docs/            architecture and stable project contracts
.agents/skills/  task-specific agent guidance
```

Keep modules narrow. Do not create generic utility dumping grounds.

## Documentation precedence

- `AGENTS.md`: engineering constitution and scope.
- `docs/PHASES.md`: implementation order and exit criteria.
- `docs/ARCHITECTURE.md`: component boundaries and data flow.
- `docs/CGROUP_V2.md`: exact metric semantics and parser contracts.
- `docs/IPC_PROTOCOL.md`: wire contract.
- `docs/BENCHMARKING.md`: valid performance methodology.

If implementation and docs disagree, do not silently pick one. Resolve the contract explicitly.
