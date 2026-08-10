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

## status

```json
{"op":"status"}
```

Returns registration count.

## Error behavior

Errors are stable machine-readable codes. Oversized input closes only that client after its error is written. A broken client cannot grow buffers without bound or block other client slots.

## Compatibility

Protocol and daemon release versions are independent. MyPaaS must negotiate with `hello`.
