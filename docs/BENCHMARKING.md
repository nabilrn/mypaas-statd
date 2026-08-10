# Benchmarking

Performance claims in this repository require reproducible evidence.

## Primary question

Does statd materially reduce control-plane runtime telemetry overhead versus the current MyPaaS approach while preserving useful metric freshness and correctness?

## Comparison targets

Where practical compare:
1. current MyPaaS Docker CLI metrics path;
2. optimized Go / Docker Engine API path if available;
3. mypaas-statd cgroup v2 path.

Do not claim statd wins merely because it is written in C.

## Workloads

Representative registration counts should include at least 1, 10, and 50 runtimes. Test 100 only when the host can support it fairly. Client counts should include one and multiple consumers when relevant. Hold sampling freshness comparable.

## Metrics

Record where practical:
- request p50/p95/p99 latency;
- statd daemon CPU consumption and RSS;
- Docker CLI child CPU;
- Docker daemon CPU/RSS when its PID is supplied;
- processes spawned per recorded request;
- process context-switch deltas when available;
- metric age/freshness only when the protocol provides enough information to measure it honestly;
- error behavior during runtime replacement/disappearance.

## Method

- document CPU/kernel/compiler/build flags;
- use release-like `-O2` for comparisons;
- warm up before recorded samples;
- collect enough iterations for percentile claims;
- repeat trials and retain machine-readable output;
- do not compare results from materially different host load/freshness settings.

## Phase 4 host harness

For one runtime:

```bash
container=my-running-container
container_pid="$(docker inspect --format '{{.State.Pid}}' "$container")"
statd_pid="$(pidof mypaas-statd)"
dockerd_pid="$(pidof dockerd)"

python3 benchmarks/compare.py \
  --container "$container" \
  --runtime-id 11111111-2222-3333-4444-555555555555:app \
  --pid "$container_pid" \
  --statd-pid "$statd_pid" \
  --docker-daemon-pid "$dockerd_pid" \
  --iterations 500
```

The harness compares:
- `docker stats --no-stream` as the existing CLI-style baseline;
- the current production statd client model: Unix connect + hello + cached snapshot.

Output includes latency distribution, measured Docker CLI process spawns, Docker CLI child CPU, and optional statd/dockerd CPU/RSS/context-switch deltas when their PIDs are supplied.

The protocol currently does not expose the sampler timestamp. The harness therefore does **not** invent metric-age/freshness numbers; that remains a separate acceptance check until the protocol can support it without widening scope purely for benchmarking.

`make test-benchmark-harness` syntax-checks and unit-tests the helper logic. Real performance execution is intentionally not a CI gate because shared runners do not represent the production MyPaaS host.

## Acceptance philosophy

If optimized Go is nearly as efficient and substantially simpler operationally, choose the simpler architecture. statd exists only if measured system behavior/capability justifies the native daemon.

Phase 4 remains **in progress** until a real target-host benchmark is recorded and Compose normal-path telemetry no longer requires Docker process discovery on every refresh.
