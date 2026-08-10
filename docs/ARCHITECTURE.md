# Architecture

## Purpose

`mypaas-statd` separates high-frequency Linux runtime telemetry from the MyPaaS Go control plane. MyPaaS remains responsible for projects, deployments, Docker lifecycle, database state, routing, authentication, and user-facing APIs. statd owns only kernel-facing metric sampling and local snapshot serving.

## Boundary

```text
MyPaaS Go control plane
        |
        | AF_UNIX / protocol v1
        v
  mypaas-statd
        |
        | bounded reads
        v
 Linux cgroup v2
```

The control plane resolves runtime identity and supplies a stable runtime identifier plus a cgroup path relative to statd's configured cgroup root. statd independently enforces that path components stay beneath the root.

## Components

### IPC server
Accepts local clients, validates bounded protocol messages, performs registration/unregistration/snapshot operations, and serializes responses. It must never trigger a full synchronous metrics sweep solely because a dashboard requested a snapshot.

Implementation begins in Phase 3.

### Registry
Phase 2 uses a fixed-capacity registry of 64 runtime entries with runtime IDs limited to 127 bytes. Each active entry owns one safely opened cgroup directory file descriptor, previous CPU sample state, and the latest successful snapshot. No heap-backed dynamically growing registry is required for v0.1.

Duplicate registration IDs are rejected rather than silently replaced. Unregister is idempotent. These semantics remain subject to the Phase 3 protocol contract.

### cgroup reader
The configured cgroup root is trusted administrator configuration. Runtime cgroup paths are relative to that root. statd opens each path component with `openat()` plus `O_DIRECTORY`, `O_NOFOLLOW`, and `O_CLOEXEC`; absolute paths, `.`/`..`, empty components, and symlink components are rejected. This avoids guessing Docker-specific filesystem layout and avoids pathname traversal through symlinks.

Controller files are opened relative to the already validated cgroup directory. Each file is read with a fixed 4096-byte bound and closed after the sample. Persistent per-controller file descriptors are not introduced without measurement.

### Sampler
The sampler reads one registered cgroup snapshot, timestamps it with `CLOCK_MONOTONIC`, and computes CPU utilization from consecutive `usage_usec` samples. The first sample, a non-positive elapsed interval, or a regressed CPU counter establishes a new baseline and reports CPU percentage as unavailable for that sample.

The Phase 2 API is explicitly tick-driven; the daemon's periodic scheduling loop is added only when the runtime server is assembled. This keeps sampler state deterministic and unit-testable.

### Parsers
Pure bounded functions parse controller text without file I/O or heap allocation. Parser tests use deterministic fixtures and do not require a live cgroup hierarchy.

## Data flow

```text
register runtime
    |
    v
open relative cgroup path safely beneath root
    |
    v
fixed registry entry + cgroup dir fd
    |
 sampler tick
    |
    +--> cpu.stat / cpu.max
    +--> memory.current / memory.max / memory.events
    +--> pids.current / pids.max
    |
    v
latest snapshot
    |
 snapshot request (Phase 3)
    v
Unix socket response
```

## Failure isolation

One malformed/disappeared cgroup must not crash the daemon or invalidate unrelated registrations. A failed read does not replace the last good snapshot with believable zeroes.

A malformed client message terminates or rejects that request/client according to protocol rules; it must not terminate the daemon.

## Resource bounds

Current v0.1 bounds established by Phase 2:
- maximum registrations: 64;
- maximum runtime ID bytes: 127;
- maximum relative cgroup path bytes: 4096;
- maximum controller file bytes: 4096.

Phase 3 will establish explicit client/message/output limits before the IPC server is considered complete. No network listener is required.

## Future expansion

Potential future metrics include I/O and PSI. eBPF/process events/health probing are separate architecture decisions and are not implied by this daemon's existence.
