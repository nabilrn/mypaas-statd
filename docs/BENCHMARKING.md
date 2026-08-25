# Performance measurement

This repository includes a local comparison harness for checking whether changes to `mypaas-statd` introduce meaningful overhead or regressions.

The harness is an engineering tool. Its output is **not a MyPaaS capacity claim**.

## Comparison

Where useful, compare:

1. the existing Docker-compatible CLI metrics path;
2. an engine/API path when available;
3. the `mypaas-statd` Unix-socket path.

Do not assume the C implementation is faster merely because it is native code.

## Method

- record the host, kernel, compiler, runtime, and build flags;
- keep sampling behavior comparable;
- warm up before recorded samples;
- repeat runs when making latency comparisons;
- record errors and correctness failures, not only timing;
- compare cgroup-derived values against the relevant Linux counters;
- do not compare runs from materially different environments as though they were equivalent.

Example:

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

`make test-benchmark-harness` checks the helper logic. Performance execution is intentionally not a shared-runner CI gate.

Keep generated result files outside the repository unless they are required for a specific review. If an optimization is not material enough to justify its operational complexity, prefer the simpler implementation.
