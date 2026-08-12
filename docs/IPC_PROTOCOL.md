# IPC Protocol v1

Status: implemented and integration-ready.

## Transport

- Linux `AF_UNIX` pathname socket;
- `SOCK_STREAM`;
- default path: `/run/mypaas/statd.sock`;
- UTF-8 JSON objects delimited by newline;
- maximum encoded request line: 8 KiB;
- maximum simultaneous clients: 8;
- socket mode after bind: `0600`.

## v1 request subset

To keep the privileged daemon parser small and auditable, v1 request objects use a strict JSON subset. Allowed keys are `op`, `protocol`, `id`, `pid`, and `cgroup`. String keys/values are printable unescaped ASCII; unknown/duplicate fields and embedded NUL are rejected.

## Handshake

```json
{"op":"hello","protocol":1}
```

Every connection must successfully negotiate protocol 1 before other operations.

## register

Production MyPaaS registration uses the host PID reported by Docker once during runtime lifecycle transitions:
```json
{"op":"register","id":"runtime-id","pid":12345}
```

statd reads `/proc/<pid>/cgroup` on the host and selects the cgroup v2 entry documented by Linux as `0::$PATH`. The root cgroup, deleted membership, malformed paths, and traversal-like components are rejected. statd does not infer Docker/systemd cgroup directory naming.

For deterministic tests and local administrative diagnostics, v1 also accepts an explicit already-resolved relative cgroup path:
```json
{"op":"register","id":"runtime-id","cgroup":"system.slice/runtime.scope"}
```

Exactly one of `pid` or `cgroup` is required. The production Go client must use `pid`.

Registration performs one initial sample. Duplicate IDs are rejected.

## unregister

```json
{"op":"unregister","id":"runtime-id"}
```

Unregister is idempotent.

## snapshot

```json
{"op":"snapshot","id":"runtime-id"}
```

Unlimited limits are JSON `null`; CPU percent is `null` until a valid delta exists. `stale:true` means the last good snapshot is returned after a later sampling failure. Snapshot never triggers a full metrics sweep.

## host_snapshot

Phase 6 adds an additive protocol-v1 operation for host-level dashboard telemetry:

```json
{"op":"host_snapshot"}
```

The operation does not take a runtime `id` and does not read procfs, sysfs, or the filesystem in the request path. The daemon samples host telemetry in the existing periodic sampler loop and the request returns the latest accepted snapshot.

Example response when both sources are valid:

```json
{"ok":true,"protocol":1,"storage":{"total_bytes":85899345920,"available_bytes":61847529062},"network":{"interface":"eth0","rx_bytes":18374829374,"tx_bytes":7391847291}}
```

Storage and network are independently nullable. For example, a host without a usable default route may return:

```json
{"ok":true,"protocol":1,"storage":{"total_bytes":85899345920,"available_bytes":61847529062},"network":null}
```

If no host sample has ever produced either a valid storage or network section, the operation returns `HOST_METRICS_UNAVAILABLE`.

Network values are cumulative counters. Protocol v1 does not manufacture a bytes-per-second rate; callers derive rates from successive snapshots and elapsed time.

## status

```json
{"op":"status"}
```

Returns registration count.

## Error behavior

Errors are stable machine-readable codes. Oversized input closes only that client after its error is written. A broken client cannot grow buffers without bound or block other client slots.

Phase 6 adds `HOST_METRICS_UNAVAILABLE` for `host_snapshot` before any valid host sample exists. Existing v1 error codes and runtime operations are unchanged.

## Compatibility

Protocol and daemon release versions are independent. MyPaaS must negotiate with `hello`.

`host_snapshot` is additive to protocol 1. A v0.1 daemon that does not know the operation may reject it while continuing to serve existing protocol-v1 runtime operations; MyPaaS integration must therefore treat host storage/network telemetry as optional during staged upgrades.
