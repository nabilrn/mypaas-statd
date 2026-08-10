# IPC Protocol v1

Status: implemented through Phase 4 registration integration and gated by tests.

## Transport

- Linux `AF_UNIX` pathname socket;
- `SOCK_STREAM`;
- default path `/run/mypaas/statd.sock`;
- newline-delimited UTF-8 JSON subset;
- request bound 8 KiB, response buffer 4 KiB;
- maximum 8 simultaneous clients;
- socket mode `0600`.

The stream does not preserve message boundaries, so the implementation handles fragmented requests, batched lines, and partial writes.

## Canonical request subset

The privileged parser is deliberately not a general JSON implementation.
- accepted keys: `op`, `protocol`, `id`, `cgroup`, `pid`;
- printable unescaped ASCII strings only;
- unknown/duplicate fields rejected;
- unsigned decimal numeric fields;
- each object ends with `\n`;
- embedded NUL rejected.

## hello

```json
{"op":"hello","protocol":1}
```

Success:
```json
{"ok":true,"protocol":1,"agent":"mypaas-statd","version":"0.1.0-dev"}
```

Every connection must negotiate before any other operation. Wrong protocol returns `UNSUPPORTED_PROTOCOL`; other operations before hello return `HELLO_REQUIRED`.

## register

`register` takes a stable runtime ID and **exactly one** locator.

Direct relative cgroup path:
```json
{"op":"register","id":"runtime-id","cgroup":"system.slice/runtime.scope"}
```

Host PID:
```json
{"op":"register","id":"runtime-id","pid":4321}
```

Rules:
- `id` is non-empty and at most 127 bytes;
- specifying both `cgroup` and `pid`, or neither, is `INVALID_REQUEST`;
- `pid` must be positive and is resolved by statd through its configured proc root (`/proc` by default);
- statd reads the unified cgroup-v2 entry from `/proc/<pid>/cgroup`, then registers the resulting relative path through the same safe cgroup reader;
- direct cgroup paths are relative to configured cgroup root and are validated component-by-component;
- duplicate IDs are rejected; the control plane can explicitly unregister then register when replacing a runtime;
- successful registration performs an initial sample; collection failure never fabricates zero metrics.

PID lookup errors are explicit: `PID_NOT_FOUND` when the process/cgroup record disappeared and `CGROUP_RESOLVE_FAILED` for invalid/unsupported resolution failures.

## unregister

```json
{"op":"unregister","id":"runtime-id"}
```

Idempotent.

## snapshot

```json
{"op":"snapshot","id":"runtime-id"}
```

The request returns the latest cached snapshot only; it never triggers a sweep. Unlimited numeric limits serialize as JSON `null`; CPU percent is `null` until a valid delta exists. `stale:true` means the last good snapshot is returned after the latest collection failed.

Unknown IDs return `NOT_FOUND`; registrations with no successful sample return `METRICS_UNAVAILABLE`.

## status

```json
{"op":"status"}
```

Returns current registration count only. This is not a generic diagnostics API.

## Error behavior

Errors use stable codes, for example:
```json
{"ok":false,"error":{"code":"INVALID_REQUEST"}}
```

Client protocol failures do not terminate the daemon. Oversized input returns `MESSAGE_TOO_LARGE` and closes that client after writing the error. Per-client buffers are fixed and slow/broken clients are isolated.

## Compatibility

Protocol versions are independent from daemon release versions. MyPaaS must negotiate protocol compatibility instead of assuming `latest` compatibility.
