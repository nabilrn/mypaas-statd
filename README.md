# mypaas-statd

`mypaas-statd` is a small Linux-native runtime telemetry daemon for MyPaaS.

Its job is intentionally narrow: sample cgroup v2 runtime counters with low overhead, keep the latest bounded snapshot in memory, and expose that snapshot to the MyPaaS Go control plane over a Unix domain socket.

## Status

Bootstrap phase. The repository currently defines architecture, engineering constraints, protocol direction, build hygiene, and a compile-clean C17 skeleton. cgroup parsing and runtime registration are intentionally deferred until the contracts in `docs/` are implemented with fixtures and tests.

## v0.1 scope

- Linux only
- C17
- cgroup v2 only
- persistent sampler
- CPU, memory, and PID metrics
- Unix domain socket IPC
- bounded memory usage
- systemd-friendly foreground daemon

## Explicit non-goals for v0.1

- eBPF
- io_uring
- shared-memory IPC
- Docker CLI or Docker SDK integration
- container lifecycle management
- HTTP server
- health probing
- log collection
- custom allocators or lock-free queues

## Development

```bash
make
make test
make sanitize
make lint
```

Read `AGENTS.md` before making changes. Kernel-facing behavior must be verified against the Linux interface being used; do not implement cgroup/procfs/socket semantics from memory alone.
