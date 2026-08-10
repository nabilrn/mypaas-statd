# Skill: mypaas-cgroup-v2

Use this skill whenever implementing or reviewing cgroup v2 metric collection.

## Contract first

Read `docs/CGROUP_V2.md`. Do not substitute remembered Docker/cgroup layouts for the project contract.

statd receives a cgroup path from MyPaaS. It validates that path under the configured cgroup root and reads controller files. It does not discover containers by invoking Docker and does not assume a Docker-specific directory layout.

## Metric classes

Know whether a source is a gauge, cumulative counter, limit, or event counter.

Examples used by this project:
- `memory.current`: gauge in bytes.
- `memory.max`: limit in bytes or literal `max`.
- `memory.events`: keyed cumulative event counters.
- `pids.current`: gauge.
- `pids.max`: limit or literal `max`.
- `cpu.stat` `usage_usec`: cumulative CPU-time counter.
- `cpu.max`: quota/period metadata; first token can be `max`.

Never expose `cpu.stat usage_usec` itself as CPU percent.

## CPU-rate calculation

CPU utilization requires at least two samples:

`usage_delta / elapsed_monotonic_time`.

With microsecond CPU usage and microsecond elapsed time, one fully busy CPU corresponds to approximately 100%. Values above 100% can be valid for multi-CPU workloads unless the API contract normalizes against an assigned CPU quota. Do not silently clamp at 100% in the collector.

Initial samples without a previous value must be marked unavailable/initial rather than fabricated as zero utilization unless the protocol contract explicitly chooses otherwise.

Counter decreases can indicate cgroup replacement/reset. Treat them as a reset and re-baseline; do not underflow unsigned arithmetic.

## Limits and `max`

Files such as `memory.max`, `pids.max`, and the quota token of `cpu.max` may use the literal `max`. Represent unlimited explicitly; never parse `max` as zero.

## Controller availability

Do not assume every optional file exists on every valid target. Required/optional status must be defined in `docs/CGROUP_V2.md`. Unsupported controller data should not make unrelated metrics incorrect.

## File reading

Pseudo-files are small but still require bounded reads. Reject unexpectedly oversized content rather than allocating without bound.

Do not keep pseudo-file FDs permanently open until verified behavior and benchmarks justify it. Correctness around cgroup replacement/deletion comes first.

## Required tests

For every parser add fixtures/tests covering:
- ordinary values;
- whitespace/newline variants allowed by the interface;
- `max` where supported;
- missing required key;
- unknown extra key;
- malformed integer;
- overflow;
- counter reset/decrease;
- zero elapsed interval protection.
