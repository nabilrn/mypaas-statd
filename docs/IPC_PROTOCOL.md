# IPC Protocol v1

Status: implemented in Phase 3 and gated by integration tests.

## Transport

- Linux `AF_UNIX` pathname socket;
- `SOCK_STREAM`;
- default path: `/run/mypaas/statd.sock`;
- UTF-8 JSON objects delimited by newline;
- maximum encoded request line: 8 KiB;
- maximum simultaneous clients: 8;
- socket mode after bind: `0600`.

The server does not listen on TCP. Because the stream transport does not preserve message boundaries, clients may fragment a request across writes or batch several newline-delimited requests in one write.

## v1 request subset

To keep the privileged daemon parser small and auditable, v1 request objects use a deliberately strict JSON subset:
- object keys are `op`, `protocol`, `id`, and `cgroup` only;
- string keys/values are printable unescaped ASCII; JSON string escape sequences are not accepted in v1;
- unknown and duplicate fields are rejected;
- numeric `protocol` is unsigned decimal;
- every request is one JSON object followed by `\n`;
- embedded NUL is invalid.

This is a protocol constraint, not a claim to implement a general JSON parser. MyPaaS controls the client and can emit this canonical representation directly.

## Handshake

Every new connection starts unnegotiated.

Request:
```json
{"op":"hello","protocol":1}
```

Response:
```json
{"ok":true,"protocol":1,"agent":"mypaas-statd","version":"0.1.0-dev"}
```

Any non-hello operation before a successful handshake returns `HELLO_REQUIRED`. A protocol mismatch returns `UNSUPPORTED_PROTOCOL`.

## register

```json
{"op":"register","id":"runtime-id","cgroup":"system.slice/runtime.scope"}
```

Requirements and semantics:
- `id` is a non-empty stable lookup key supplied by MyPaaS, maximum 127 bytes;
- presentation metadata such as service name remains owned by the Go control plane and is not duplicated in statd v1;
- `cgroup` is a non-empty path relative to statd's configured cgroup root;
- Phase 2 validation rejects absolute paths, `.`/`..`, empty components, and symlink traversal;
- duplicate IDs are rejected;
- successful registration performs one initial sample of that runtime so a snapshot can normally be served immediately; failure of that initial sample does not fabricate metrics.

## unregister

```json
{"op":"unregister","id":"runtime-id"}
```

Unregister is idempotent.

## snapshot

```json
{"op":"snapshot","id":"runtime-id"}
```

A successful response includes typed CPU, memory, and PID values. Unlimited limits are JSON `null`, not numeric zero. CPU percent is `null` until a valid delta exists. `stale:true` means a previous valid snapshot is being returned after the most recent sampling attempt failed.

If no valid snapshot has ever been collected, the response is `METRICS_UNAVAILABLE`; unknown IDs return `NOT_FOUND`.

A snapshot request only reads the latest in-memory snapshot; it does not trigger a metrics sweep.

## status

```json
{"op":"status"}
```

Returns the current registration count. It is intentionally small and is not a general diagnostics endpoint.

## Error behavior

Errors use stable machine-readable codes, for example:
```json
{"ok":false,"error":{"code":"INVALID_REQUEST"}}
```

A malformed request does not terminate the daemon. Oversized input returns `MESSAGE_TOO_LARGE` and closes that client after the error is written. A broken/slow client cannot create unbounded per-client buffers or block other client slots.

## Compatibility

Protocol changes are versioned independently from daemon release versions. MyPaaS must perform the hello negotiation instead of assuming `latest` is compatible.
