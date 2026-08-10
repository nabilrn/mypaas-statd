# Architecture

## Purpose

`mypaas-statd` removes repeated Docker CLI/process work from MyPaaS runtime telemetry. MyPaaS remains responsible for project/deployment state, Docker lifecycle, routing, authentication, presentation metadata, and user-facing APIs. statd owns kernel-facing metric sampling and local cached snapshot serving.

## Boundary

```text
MyPaaS Go control plane
        |
        | runtime id + host PID (preferred integration)
        | or explicit relative cgroup path
        | AF_UNIX protocol v1
        v
  mypaas-statd (host)
        |
        +--> /proc/<pid>/cgroup  [cold registration path]
        |
        +--> /sys/fs/cgroup/... [periodic hot sampling path]
```

The Go control plane can obtain the container's host PID from the container runtime during registration/reconciliation. statd uses host procfs to resolve cgroup membership, avoiding a host `/proc` mount inside the containerized MyPaaS API. Direct relative cgroup registration remains supported for controlled integrations/tests.

statd never infers Docker/systemd cgroup directory naming.

## Components

### IPC server
A nonblocking AF_UNIX listener uses `poll()` over a fixed maximum of eight clients. Accepted sockets are nonblocking/CLOEXEC. Fixed input/output buffers handle partial stream reads/writes. This is sufficient for the expected local control-plane workload; epoll/threads are intentionally absent.

### Runtime registration
`register` carries a stable runtime ID plus exactly one locator: relative cgroup path or host PID.

For PID registration, the proc resolver opens the configured proc root (default `/proc`), opens the numeric PID directory with no-follow semantics, reads its bounded `cgroup` file, selects the unified cgroup-v2 entry, then passes the resulting relative path into the ordinary cgroup registration flow.

PID resolution is cold-path work. Snapshot requests never reread procfs.

### Registry
Fixed capacity: 64 runtime entries. Each entry owns runtime ID, safely opened cgroup directory FD, previous CPU sample state, last sample result, and latest good snapshot. Duplicate IDs are rejected; unregister is idempotent.

### cgroup reader
The cgroup root is trusted administrator configuration. Runtime paths are relative. Components are opened with `openat()` + directory/no-follow/CLOEXEC semantics; absolute paths, `.`, `..`, empty components, and symlink components are rejected. Controller files are bounded to 4096 bytes and currently opened/read/closed per sample; persistent controller FDs are deferred unless measurement justifies them.

### Sampler
Uses `CLOCK_MONOTONIC`. CPU utilization is derived from consecutive `usage_usec` samples; first sample, non-positive elapsed time, or regressed counter creates a new baseline with unavailable percent. Sampling runs periodically and independently from client requests. Failed samples preserve the last good snapshot and expose staleness rather than believable zeroes.

### Parsers
Pure bounded parsers for cgroup controller text and `/proc/<pid>/cgroup` text. They do no heap allocation and are testable with deterministic strings/filesystems.

## Data flow

```text
Docker inspect / lifecycle reconciliation
        |
        | host PID
        v
statd register
        |
        v
/proc/<pid>/cgroup
        |
        v
validated relative cgroup path
        |
        v
open cgroup directory once
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
        v
cheap AF_UNIX snapshot request
```

## Failure isolation

A disappeared PID/cgroup or malformed controller file affects only that registration/sample. Client protocol failures affect only the client slot. Failed reads never overwrite good metrics with zeros.

MyPaaS integration must keep a Docker-based fallback until deployment/benchmark evidence proves statd operationally reliable.

## Resource bounds

Current v0.1 bounds:
- registrations: 64;
- runtime ID: 127 bytes;
- relative cgroup path: 4096 bytes;
- proc cgroup file: 8192 bytes;
- controller file: 4096 bytes;
- simultaneous IPC clients: 8;
- IPC request: 8192 bytes;
- per-client response buffer: 4096 bytes.

Increase bounds only for demonstrated MyPaaS requirements.

## Future expansion

I/O, PSI, OOM notifications, health probing, and event-driven lifecycle work require separate need/contracts/benchmarks. Do not pre-build abstractions for them.
