# Operations

`mypaas-statd` is designed to run as a small host service next to Docker, not as a container.

## Prerequisites

- Linux with cgroup v2 mounted at `/sys/fs/cgroup`;
- a C17 compiler for source builds;
- systemd for the provided service unit;
- MyPaaS API running as root inside its current production container so it can access the root-owned `0600` Unix socket through the `/run/mypaas` bind mount.

Confirm cgroup v2 before installation:

```bash
test -f /sys/fs/cgroup/cgroup.controllers
```

## Build and install from source

```bash
make
make test
make sanitize
make lint
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable --now mypaas-statd
```

The default installation places:

```text
/usr/local/bin/mypaas-statd
/usr/local/lib/systemd/system/mypaas-statd.service
/run/mypaas/statd.sock
```

`/run/mypaas` is created by systemd through `RuntimeDirectory=mypaas`.

## Verify the daemon

```bash
systemctl status mypaas-statd
ls -l /run/mypaas/statd.sock
```

The socket is intentionally local-only and mode `0600`. statd does not listen on TCP.

## Enable in MyPaaS

The MyPaaS production Compose file bind-mounts `/run/mypaas` into the API container, but statd remains opt-in.

Set in the MyPaaS `.env` only after the host daemon is healthy:

```bash
STATD_SOCKET=/run/mypaas/statd.sock
```

Then recreate the API container so it receives the environment variable:

```bash
docker compose -f docker-compose.prod.yml up -d api
```

If `STATD_SOCKET` is empty, MyPaaS uses the existing Docker CLI metrics path. If statd is configured but an operation fails, the current integration also falls back to Docker metrics rather than failing the dashboard request.

## Roll back statd usage

No uninstall is required for an application rollback. Clear the MyPaaS setting and recreate the API container:

```bash
STATD_SOCKET=
docker compose -f docker-compose.prod.yml up -d api
```

The statd daemon may remain installed while MyPaaS ignores it.

## Benchmark before claiming improvement

Use a real running MyPaaS workload on the target VM. For a single container:

```bash
container=my-running-container
pid="$(docker inspect --format '{{.State.Pid}}' "$container")"

python3 benchmarks/compare.py \
  --container "$container" \
  --runtime-id bench:app \
  --pid "$pid" \
  --iterations 500
```

Run several times under comparable host load and retain the JSON output. The benchmark harness measures the existing `docker stats --no-stream` style baseline against the current statd client model (Unix connect + hello + snapshot).

## Current packaging policy

Phase 4 uses source build + systemd installation. GitHub Release binaries and any registry/image publishing are deliberately deferred until v0.1 hardening is complete.
