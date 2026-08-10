# Skill: mypaas-low-latency-linux

Use this skill for performance-sensitive Linux design decisions in `mypaas-statd`.

## Philosophy

Low latency is primarily achieved by removing expensive work and contention, not by maximizing mechanism complexity.

Preferred escalation order:
1. simple synchronous syscall;
2. bounded state reuse;
3. persistent local socket/descriptor where lifecycle semantics are clear;
4. `poll` when a small set of FDs needs multiplexing;
5. `epoll` when connection count/behavior justifies it;
6. `timerfd`/`eventfd` when integrating timing/wakeup into the event loop clearly simplifies behavior;
7. shared memory, io_uring, eBPF, custom allocators, lock-free structures only after benchmark evidence and an explicit architecture decision.

## Hot-path rules

- No process creation.
- No shell commands.
- No unbounded allocation.
- No repeated parsing of static configuration when it can safely be prepared once.
- No successful-sample logging.
- Avoid global locks; but do not replace a simple lock with a complex lock-free design without evidence.
- Batch work when it reduces syscalls without harming freshness or correctness.

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

## Benchmark interpretation

A lower microbenchmark number is not automatically a better system. Prefer the simpler implementation when differences are immaterial to the MyPaaS workload.
