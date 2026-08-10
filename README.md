# mypaas-statd

`mypaas-statd` is a small Linux-native runtime telemetry daemon for MyPaaS.

Its job is intentionally narrow: sample cgroup v2 runtime counters with low overhead, keep the latest bounded snapshot in memory, and expose that snapshot to the MyPaaS Go control plane over a Unix domain socket.

## Status

Phases 0–3 are complete and CI-gated. Phase 4 integration is implemented through the MyPaaS statd client branch, host PID-based cgroup resolution, optional production socket wiring, systemd packaging, and a real-host benchmark harness.

The Phase 4 **real-host performance gate is accepted** based on preserved Debian 13 + Podman evidence from 3 comparable trials × 500 recorded iterations. The tested Docker-compatible CLI path averaged about 43.03 ms per request across the three trials, while the statd socket path averaged about 0.83 ms, with 500 measured CLI process spawns per trial versus 0 client process spawns for statd. Correctness checks also compared statd output against raw cgroup v2 counters and exercised runtime disappearance without crashing the daemon.

The raw evidence is preserved under `benchmarks/results/phase4-debian13-podman-2026-08-10/`. Phase 4 overall still requires the MyPaaS integration branch to be synchronized, reviewed, merged, and exercised end-to-end before production rollout is considered complete.

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
