# Phase 4 Real-Host Evidence: Debian 13 + Podman

This directory preserves real-host Phase 4 benchmark evidence for `mypaas-statd`.

## Scope

- Tested `mypaas-statd` commit: `cf8843545ea19ecf9a54049e21b2fe609e49d58d`
- Host: Debian GNU/Linux 13 (trixie), kernel `6.12.88+deb13-amd64`
- Runtime: rootful Podman 5.4.2
- MyPaaS Docker-compatible path: `/var/run/docker.sock -> /run/podman/podman.sock`
- Docker Engine/dockerd was not installed for this validation.

## Methodology

The benchmark used `benchmarks/compare.py` against one rootful Podman Alpine workload running a CPU loop with `--memory 128m --pids-limit 128`.

Each run used:

```bash
python3 benchmarks/compare.py \
  --container statd-bench \
  --runtime-id phase4:bench-trial-N \
  --pid "$BENCH_PID" \
  --statd-pid "$STATD_PID" \
  --warmup 50 \
  --iterations 500
```

There were 3 comparable trials with 500 recorded iterations each. Warmup samples are not included in the recorded latency distributions.

The Docker-compatible CLI baseline is `docker stats --no-stream` through the Podman-backed compatibility command/socket path. The statd path uses protocol v1 over `/run/mypaas/statd.sock`, with connect + hello + snapshot per recorded sample.

Protocol v1 does not expose a sampler timestamp, so this evidence does not make metric freshness or metric age claims.

## Correctness Checks

A rootful Podman container host PID was registered through statd protocol v1. The statd snapshot was compared against raw cgroup v2 files under the resolved container cgroup:

`/machine.slice/libpod-6585b959d640ff3d8e4044381f7df9390be4d1180d93a7a1242e727f9612c2a3.scope/container`

Observed cgroup comparison:

- `memory.max_bytes`: exact match, `134217728`
- `pids.max`: exact match, `128`
- `memory.oom`: exact match, `0`
- `memory.oom_kill`: exact match, `0`
- `pids.current`: exact match, `1`
- `cpu.period_usec`: exact match, `100000`
- `cpu.quota_usec`: exact match, `null`
- `memory.current_bytes`: exact match at comparison time
- `cpu.usage_usec`: statd cached sample was behind raw cgroup by `699357` usec, which is expected for a sampled daemon and was not ahead of raw kernel state

Runtime stop/disappearance behavior was also exercised:

- Snapshot before stopping a temporary container was valid.
- After `podman stop`, statd returned `NOT_FOUND` for that runtime ID.
- `mypaas-statd` stayed active after the container disappeared.
- After systemd restart, statd was active and registration state was empty.

## Benchmark Results

The raw machine-readable benchmark outputs are preserved without modification:

- `benchmark-run-1.json`
- `benchmark-run-2.json`
- `benchmark-run-3.json`

### Run 1

| Path | p50_ms | p95_ms | p99_ms | mean_ms | max_ms | wall_seconds | process_spawns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Docker-compatible CLI | 41.039357499999994 | 51.9851865 | 57.27985834999998 | 42.131678146 | 68.288171 | 21.067599074 | 500 |
| mypaas-statd | 0.7803485 | 0.8812820499999998 | 1.0528069899999999 | 0.793765936 | 1.255629 | 0.397203313 | 0 |

Additional run 1 observations:

- Docker CLI child CPU seconds: `19.248283`
- statd daemon CPU seconds: `0.03`
- statd RSS before: `1859584`
- statd RSS after: `1859584`
- statd voluntary context switches: `1973`
- statd involuntary context switches: `27`

### Run 2

| Path | p50_ms | p95_ms | p99_ms | mean_ms | max_ms | wall_seconds | process_spawns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Docker-compatible CLI | 41.5558045 | 53.618609249999956 | 64.31991137 | 43.004707342 | 72.804908 | 21.504005963 | 500 |
| mypaas-statd | 0.7968685 | 1.0064879999999998 | 1.1074274999999998 | 0.826836158 | 4.199228 | 0.413757872 | 0 |

Additional run 2 observations:

- Docker CLI child CPU seconds: `19.720067`
- statd daemon CPU seconds: `0.03`
- statd RSS before: `1859584`
- statd RSS after: `1859584`
- statd voluntary context switches: `1963`
- statd involuntary context switches: `30`

### Run 3

| Path | p50_ms | p95_ms | p99_ms | mean_ms | max_ms | wall_seconds | process_spawns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Docker-compatible CLI | 43.2664495 | 55.993713 | 64.04532325 | 43.954954898000004 | 68.152107 | 21.978995991 | 500 |
| mypaas-statd | 0.820184 | 1.08817145 | 1.2006058099999999 | 0.870551032 | 1.544936 | 0.43590697 | 0 |

Additional run 3 observations:

- Docker CLI child CPU seconds: `19.956553`
- statd daemon CPU seconds: `0.03999999999999998`
- statd RSS before: `1859584`
- statd RSS after: `1859584`
- statd voluntary context switches: `1982`
- statd involuntary context switches: `12`

## Conclusion

This evidence supports accepting the Phase 4 real-host performance gate for the tested Podman-backed Docker-compatible CLI baseline. The native daemon preserved the checked cgroup v2 CPU, memory, and PID semantics, handled container disappearance without crashing, and materially reduced measured telemetry overhead in all three recorded trials.

This conclusion is limited to the collected evidence in this directory.
