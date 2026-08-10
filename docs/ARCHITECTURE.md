# Architecture

## Purpose

`mypaas-statd` separates high-frequency Linux runtime telemetry from the MyPaaS Go control plane. MyPaaS remains responsible for projects, deployments, Docker lifecycle, database state, routing, authentication, presentation metadata, and user-facing APIs. statd owns only kernel-facing metric sampling and local snapshot serving.

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
Phase 3 uses one nonblocking AF_UNIX listener plus `poll()` over a fixed maximum of eight client slots. Accepted sockets are explicitly nonblocking and close-on-exec. Each client has fixed input/output buffers; partial stream reads/writes are handled without heap growth. A client with pending output is not allowed to accumulate another unbounded request stream.

The server implements only hello/version negotiation, register, unregister, latest snapshot, and registration-count status. It does not expose TCP, HTTP, gRPC, or a generic command framework.

### Registry
The fixed-capacity registry contains 64 runtime entries with runtime IDs limited to 127 bytes. Each active entry owns one safely opened cgroup directory file descriptor, previous CPU sample state, last sampling result, and latest successful snapshot. No heap-backed dynamically growing registry is required for v0.1.

Duplicate registration IDs are rejected. Unregister is idempotent.

### cgroup reader
The configured cgroup root is trusted administrator configuration. Runtime cgroup paths are relative to that root. statd opens each path component with `openat()` plus `O_DIRECTORY`, `O_NOFOLLOW`, and `O_CLOEXEC`; absolute paths, `.`/`..`, empty components, and symlink components are rejected.

Controller files are opened relative to the validated cgroup directory. Each file is read with a fixed 4096-byte bound and closed after the sample. Persistent per-controller file descriptors are not introduced without measurement.

### Sampler
The sampler reads registered cgroups, timestamps samples with `CLOCK_MONOTONIC`, and computes CPU utilization from consecutive `usage_usec` samples. The first sample, a non-positive elapsed interval, or a regressed CPU counter establishes a new baseline and reports CPU percentage as unavailable.

The daemon performs periodic sampling independently of client snapshot requests. Registration performs one initial per-runtime sample; normal snapshot requests return the latest cached result. A failed sample records its status but preserves the last good snapshot, which the IPC layer can mark stale.

### Parsers
Pure bounded functions parse controller text without file I/O or heap allocation.

## Data flow

```text
register runtime
    |
    v
safe relative cgroup open
    |
    v
fixed registry entry + cgroup dir fd
    |
 periodic sampler (~1 s)
    |
    +--> cpu.stat / cpu.max
    +--> memory.current / memory.max / memory.events
    +--> pids.current / pids.max
    |
    v
latest snapshot + last sample status
    |
 snapshot request
    v
poll-based Unix socket response
```

## Failure isolation

One malformed/disappeared cgroup must not crash the daemon or invalidate unrelated registrations. A failed read does not replace the last good snapshot with believable zeroes.

Malformed, oversized, disconnected, or broken IPC clients are isolated to their fixed client slot. Runtime client errors do not terminate the daemon.

## Resource bounds

Current v0.1 bounds:
- registrations: 64;
- runtime ID: 127 bytes;
- relative cgroup path: 4096 bytes;
- controller file: 4096 bytes;
- simultaneous IPC clients: 8;
- IPC request: 8192 bytes;
- per-client response buffer: 4096 bytes.

These bounds are intentionally conservative and may only increase when the actual MyPaaS integration needs it.

## Future expansion

Potential future metrics include I/O and PSI. eBPF/process events/health probing are separate architecture decisions and are not implied by this daemon's existence.
