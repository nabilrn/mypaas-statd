# IPC Protocol v1

Status: design contract for v0.1; implementation has not started.

## Transport

- `AF_UNIX`
- `SOCK_STREAM`
- default path: `/run/mypaas/statd.sock`
- UTF-8 newline-delimited JSON (one object per line)
- maximum encoded request/response message: 64 KiB unless deliberately revised

The server does not listen on TCP.

## General response shape

Success:
```json
{"ok":true,"protocol":1}
```

Failure:
```json
{"ok":false,"error":{"code":"INVALID_REQUEST","message":"..."}}
```

Error messages must not expose arbitrary host file content.

## hello

Request:
```json
{"op":"hello","protocol":1}
```

Response:
```json
{"ok":true,"protocol":1,"agent":"mypaas-statd","version":"0.1.0"}
```

A protocol mismatch returns an explicit unsupported-protocol error.

## register

Conceptual request:
```json
{"op":"register","id":"runtime-id","service":"api","cgroup":"system.slice/runtime.scope"}
```

Requirements:
- all strings bounded;
- `id` is the stable lookup key supplied by MyPaaS;
- `service` is presentation metadata only;
- `cgroup` is a path relative to statd's configured cgroup root, never an arbitrary absolute host path;
- Phase 2 path validation rejects absolute paths, `.`/`..`, empty path components, and symlink traversal;
- v0.1 duplicate IDs are rejected rather than implicitly replacing the active registration.

## unregister

```json
{"op":"unregister","id":"runtime-id"}
```

Operation is idempotent in v0.1.

## snapshot

Single runtime:
```json
{"op":"snapshot","id":"runtime-id"}
```

Response shape will carry an explicit validity state and typed CPU/memory/PID data. It must distinguish unlimited limits from numeric zero and unavailable metrics from valid zero.

A future batch snapshot operation may be added if MyPaaS needs it. Do not add protocol surface speculatively.

## Stream handling

Because SOCK_STREAM does not preserve message boundaries:
- callers must accumulate until newline;
- handle partial `recv`/`send`;
- reject a message that exceeds the configured maximum before newline;
- one slow/malformed peer must not create unbounded memory growth;
- embedded NUL bytes are invalid protocol input.

## Compatibility

Protocol changes are versioned independently from daemon release versions. MyPaaS must negotiate/check protocol compatibility instead of assuming `latest` is compatible.
