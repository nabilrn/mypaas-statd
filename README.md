# mypaas-statd

`mypaas-statd` is a small Linux-native runtime telemetry daemon for MyPaaS.

Its job is intentionally narrow: sample cgroup v2 runtime counters with low overhead, keep the latest bounded snapshot in memory, and expose that snapshot to the MyPaaS Go control plane over a Unix domain socket.

## Status

Phases 0–3 are complete and CI-gated. Phase 4 integration is implemented through the MyPaaS statd client branch, host PID-based cgroup resolution, optional production socket wiring, systemd packaging, and a real-host benchmark harness.

Phase 4 is **not considered complete yet** because the performance acceptance gate requires benchmark evidence from the actual MyPaaS VM. GitHub Actions validates correctness and build hygiene; it is not used to manufacture production latency claims.

## v0.1 scope

- Linux only
- C17
- cgroup v2 only
- persistent sampler
- CPU, memory, and PID metrics
- Unix domain socket IPC
- bounded memory/client/registration state
- host PID → cgroup v2 resolution through `/proc/<pid>/cgroup`
- systemd-hosted foreground daemon
- MyPaaS Docker-metrics fallback during rollout

## Explicit non-goals for v0.1

- eBPF
- io_uring
- shared-memory IPC
- Docker CLI or Docker SDK inside statd
- container lifecycle management inside statd
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

Staged installation is also covered by `make test`:

```bash
sudo make install
```

Operational setup is documented in `docs/OPERATIONS.md`. Benchmark methodology is in `docs/BENCHMARKING.md`.

Read `AGENTS.md` before making changes. Kernel-facing behavior must be verified against the Linux interface being used; do not implement cgroup/procfs/socket semantics from memory alone. Prefer the simplest mature implementation that satisfies the measured workload.
