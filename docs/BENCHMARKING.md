# Benchmarking

Performance claims in this repository require reproducible evidence.

## Primary question

Does statd materially reduce control-plane runtime telemetry overhead versus the current MyPaaS approach while preserving useful metric freshness and correctness?

## Comparison targets

Where practical compare:
1. current MyPaaS Docker-compatible CLI metrics path;
2. optimized Go / container-engine API path if available;
3. mypaas-statd cgroup v2 path.

Do not claim statd wins merely because it is written in C.

## Workloads

Representative registration counts should include at least 1, 10, and 50 runtimes when making broader scalability claims. Test 100 only when the host can support it fairly. Client counts should include one and multiple consumers when relevant. Hold sampling freshness comparable.

The initial Phase 4 real-host acceptance evidence intentionally covers one representative runtime and the production client request model. Multi-runtime runs remain useful capacity characterization and must be completed before making broader scale claims from this benchmark.

## Metrics

Record where practical:
- request p50/p95/p99 latency;
- statd daemon CPU consumption and RSS;
- Docker-compatible CLI child CPU;
- container-engine daemon CPU/RSS when an applicable PID is available;
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

python3 benchmarks/compare.py \
  --container "$container" \
  --runtime-id 11111111-2222-3333-4444-555555555555:app \
  --pid "$container_pid" \
  --statd-pid "$statd_pid" \
  --iterations 500
```

When the runtime is Docker Engine and `dockerd` exists, `--docker-daemon-pid` may also be supplied. The accepted Podman-backed MyPaaS evidence did not install `dockerd`; `/var/run/docker.sock` was backed by `/run/podman/podman.sock`.

The harness compares:
- `docker stats --no-stream` as the existing Docker-compatible CLI-style baseline;
- the current production statd client model: Unix connect + hello + cached snapshot.

Output includes latency distribution, measured CLI process spawns, CLI child CPU, and optional statd/container-engine CPU/RSS/context-switch deltas when their PIDs are supplied.

The protocol currently does not expose the sampler timestamp. The harness therefore does **not** invent metric-age/freshness numbers; that remains a separate acceptance check until the protocol can support it without widening scope purely for benchmarking.

`make test-benchmark-harness` syntax-checks and unit-tests the helper logic. Real performance execution is intentionally not a CI gate because shared runners do not represent the production MyPaaS host.

## Accepted Phase 4 real-host evidence

The accepted evidence is preserved at:

`benchmarks/results/phase4-debian13-podman-2026-08-10/`

Environment and scope:
- Debian GNU/Linux 13.5 on kernel `6.12.88+deb13-amd64`;
- 2 vCPU KVM guest on AMD EPYC 7642;
- cgroup v2;
- rootful Podman 5.4.2;
- Docker-compatible socket path `/var/run/docker.sock -> /run/podman/podman.sock`;
- tested statd commit `cf8843545ea19ecf9a54049e21b2fe609e49d58d`;
- 3 comparable trials × 500 recorded iterations with 50 warmup iterations.

Across the three recorded trials, the Docker-compatible CLI mean latency was approximately 43.03 ms and the statd socket mean latency was approximately 0.83 ms. Each CLI trial recorded 500 child-process spawns while statd recorded zero client process spawns. The raw JSON files remain authoritative for exact p50/p95/p99/max, CPU, RSS, and context-switch values.

Correctness checks compared statd values against raw cgroup v2 files and exercised runtime disappearance without crashing the daemon. The evidence supports accepting the Phase 4 **real-host performance gate** for the tested Podman-backed deployment model.

## Acceptance philosophy

If optimized Go is nearly as efficient and substantially simpler operationally, choose the simpler architecture. statd exists only if measured system behavior/capability justifies the native daemon.

The real-host performance gate is accepted. Phase 4 overall remains pending synchronization, final review, merge, and end-to-end validation of the MyPaaS integration branch before production rollout is considered complete.
