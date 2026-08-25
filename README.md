# mypaas-statd

`mypaas-statd` is a small Linux-native runtime telemetry daemon for MyPaaS.

Its responsibility is intentionally narrow: read cgroup v2 runtime counters, keep a bounded in-memory snapshot, and expose that snapshot to the MyPaaS Go control plane over a Unix domain socket.

## Scope

- Linux only
- C17
- cgroup v2 only
- CPU, memory, and PID metrics
- persistent sampler
- Unix domain socket IPC
- bounded client, registration, and snapshot state
- host PID to cgroup v2 resolution through `/proc/<pid>/cgroup`
- systemd-hosted foreground daemon
- MyPaaS engine-metrics fallback during rollout

## Non-goals

- container lifecycle management
- HTTP server
- health probing
- log collection
- eBPF
- io_uring
- shared-memory IPC
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

Operational setup is documented in `docs/OPERATIONS.md`.

Performance tooling may be used locally to compare implementation changes, but repository benchmark runs are not MyPaaS capacity claims and should not be presented as product marketing.

Read `AGENTS.md` before making changes. Kernel-facing behavior must be verified against the Linux interface being used; do not implement cgroup, procfs, or socket semantics from memory alone.
