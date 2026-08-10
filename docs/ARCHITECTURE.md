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

The control plane resolves runtime identity and supplies a validated registration request containing a stable runtime identifier, service label, and cgroup path. statd independently enforces that the path remains inside its configured cgroup root.

## Components

### IPC server
Accepts local clients, validates bounded protocol messages, performs registration/unregistration/snapshot operations, and serializes responses. It must never trigger a full synchronous metrics sweep solely because a dashboard requested a snapshot.

### Registry
Stores a bounded set of runtime registrations. Each entry owns identifying metadata, validated cgroup path, previous CPU sample state, and latest metric snapshot.

### Sampler
Runs at a configured interval (initial target: 1000 ms). It reads registered cgroup controller files, computes deltas from monotonic time, and atomically/logically replaces each entry's latest snapshot.

### Parsers
Pure/bounded functions for controller text. Parser tests use fixtures and do not require a live cgroup hierarchy.

## Data flow

```text
register runtime
    |
    v
validate path under cgroup root
    |
    v
registry entry
    |
 periodic sampler
    |
    +--> cpu.stat / cpu.max
    +--> memory.current / memory.max / memory.events
    +--> pids.current / pids.max
    |
    v
latest snapshot
    |
 snapshot request
    v
Unix socket response
```

## Failure isolation

One malformed/disappeared cgroup must not crash the daemon or invalidate unrelated registrations. Sampling errors are represented per runtime and can be recovered on later intervals.

A malformed client message terminates or rejects that request/client according to protocol rules; it must not terminate the daemon.

## Resource bounds

Exact defaults will be constants/config with tests, but the design requires explicit limits for:
- maximum registrations;
- maximum simultaneous clients;
- maximum IPC message bytes;
- maximum identifier/service/path lengths;
- maximum controller file bytes;
- output buffer size.

No network listener is required.

## Future expansion

Potential future metrics include I/O and PSI. eBPF/process events/health probing are separate architecture decisions and are not implied by this daemon's existence.
