# cgroup v2 Metric Contract

This document defines the project-level metric semantics. Before implementing parsers, verify each file/field against authoritative current Linux cgroup v2 documentation available in the development environment. If Linux semantics conflict with this document, stop and update the contract deliberately rather than silently coding around it.

## Root and paths

Default cgroup root: `/sys/fs/cgroup`.

The MyPaaS control plane supplies a cgroup path. statd must canonicalize/validate it and ensure the resulting target cannot escape the configured root through `..`, absolute-path substitution, or symlink traversal.

statd must not infer Docker-specific cgroup directory layouts.

## CPU

Files targeted for v0.1:
- `cpu.stat`
- `cpu.max`

Required CPU-time source: `usage_usec` from `cpu.stat`.

`usage_usec` is a cumulative counter, not a utilization percentage.

For consecutive valid samples:

```text
usage_delta_usec = current_usage_usec - previous_usage_usec
elapsed_usec = monotonic_now - previous_monotonic_time
raw_cpu_percent = usage_delta_usec / elapsed_usec * 100
```

A workload using two CPUs fully may report near 200% in raw utilization. Do not clamp collector output to 100% merely for UI convenience.

If the cumulative counter decreases or the registration identity is replaced, discard the delta and establish a new baseline.

`cpu.max` contains quota and period. The quota token may be `max`. v0.1 records the limit metadata needed by the MyPaaS metric contract; it must not fabricate a numeric quota for `max`.

## Memory

Files:
- `memory.current`
- `memory.max`
- `memory.events`

`memory.current`: current memory usage in bytes.

`memory.max`: memory limit in bytes or literal `max`. Unlimited is represented explicitly.

`memory.events`: keyed cumulative event counters. v0.1 should at least be able to represent OOM-related counters selected during implementation contract finalization. Unknown future keys should not make the parser fail if required keys remain valid.

## PIDs

Files:
- `pids.current`
- `pids.max`

`pids.current`: current number of processes/tasks accounted by the controller according to kernel semantics.

`pids.max`: numeric limit or literal `max`.

## Optional future sources

Not part of initial implementation unless explicitly promoted:
- `io.stat`
- `cpu.pressure`
- `memory.pressure`
- `io.pressure`
- `memory.peak`

## Snapshot validity

Each snapshot needs collection time based on monotonic sampling state and an explicit health/validity state. Do not replace a failed read with a believable zero.

Examples:
- zero memory usage can be a valid numeric reading;
- unavailable memory data is a different state;
- first CPU sample has no utilization delta yet;
- disappeared cgroup is not equivalent to 0% CPU / 0 bytes memory.
