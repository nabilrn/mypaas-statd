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

Representative registration counts should include at least:
- 1 runtime;
- 10 runtimes;
- 50 runtimes;
- 100 runtimes when the test host can support them fairly.

Client counts should include at least 1 and multiple concurrent snapshot consumers where applicable.

Sampling freshness/interval must be held comparable across implementations.

## Metrics

Record where possible:
- snapshot request p50/p95/p99 latency;
- sampler CPU consumption;
- daemon RSS;
- Docker daemon CPU for Docker-based comparison;
- processes spawned per second;
- syscalls/context switches where tooling is available;
- metric age at response time;
- error rate;
- behavior during cgroup deletion/replacement.

## Method

- document CPU/kernel/compiler/build flags;
- use release-like `-O2` for throughput/latency comparisons, not sanitizer builds;
- run warmup before recorded samples where appropriate;
- use enough iterations to make percentile claims meaningful;
- repeat runs and report variance rather than a single lucky number;
- retain raw output or machine-readable summaries when adding public claims.

## Acceptance philosophy

If an optimized Go implementation is nearly as efficient and substantially simpler operationally, choose the simpler architecture. statd should exist because measured system behavior and capability justify the extra native daemon, not because C is aesthetically preferred.
