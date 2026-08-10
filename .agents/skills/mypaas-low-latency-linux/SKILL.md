# Skill: mypaas-low-latency-linux

Use this skill for performance-sensitive Linux design decisions in `mypaas-statd`.

## Philosophy

Low latency is primarily achieved by removing expensive work and contention, not by maximizing mechanism complexity.

The preferred solution is the simplest mature Linux mechanism that satisfies the measured workload. If a plain syscall loop is fast enough, keep it. If a simple persistent descriptor is enough, do not introduce an event framework. Complexity must buy a demonstrated operational benefit.

Preferred escalation order:
1. simple synchronous syscall;
2. bounded state reuse;
3. persistent local socket/descriptor where lifecycle semantics are clear;
4. `poll` when a small set of FDs needs multiplexing;
5. `epoll` when connection count/behavior justifies it;
6. `timerfd`/`eventfd` when integrating timing/wakeup into the event loop clearly simplifies behavior;
7. shared memory, io_uring, eBPF, custom allocators, lock-free structures only after benchmark evidence and an explicit architecture decision.

Do not skip levels because a lower-level mechanism sounds faster.

## Simplicity acceptance rule

When two designs both satisfy correctness and latency requirements, choose the design with:
- fewer states;
- fewer syscalls only when the difference matters;
- fewer synchronization points;
- fewer lifetime relationships;
- easier failure cleanup;
- easier tests;
- more stable/documented kernel semantics.

A microbenchmark improvement that does not matter to the MyPaaS workload is not sufficient justification for additional architecture.

Before adding a performance mechanism, answer:
1. What measured problem exists?
2. What is the current baseline?
3. Why is the simpler mechanism insufficient?
4. What additional failure/lifecycle states does the proposed mechanism create?
5. What benchmark result would justify keeping it?

If these cannot be answered, do not add the mechanism.

## Hot-path rules

- No process creation.
- No shell commands.
- No unbounded allocation.
- No repeated parsing of static configuration when it can safely be prepared once.
- No successful-sample logging.
- Avoid global locks; but do not replace a simple lock with a complex lock-free design without evidence.
- Batch work when it reduces syscalls without harming freshness or correctness.
- Do not cache data whose collection is already cheap unless the cache removes a measured cost.

## Measurement

Before an optimization, record a baseline with a reproducible workload. After the change, repeat the same workload.

Measure at least where applicable:
- p50/p95/p99 snapshot latency;
- sampler CPU time;
- RSS;
- context switches;
- syscalls/sample;
- metric freshness/error;
- behavior as registrations and clients scale.

Do not compare unlike freshness intervals.

## Event-loop guidance

For v0.1, a simple design is preferred. `epoll` is justified only if handling multiple clients with blocking I/O would otherwise require threads or poor polling behavior. If used:
- nonblocking state machines must handle partial reads/writes;
- per-client buffers must be bounded;
- slow clients cannot block sampling;
- disconnect cleanup must be deterministic.

Do not design an event loop in an earlier phase merely because Phase 3 may eventually need one.

## Benchmark interpretation

A lower microbenchmark number is not automatically a better system. Prefer the simpler implementation when differences are immaterial to the MyPaaS workload.

Performance work is complete when the requirement is met with adequate headroom; continuing to optimize solely to produce a lower number is out of scope.
