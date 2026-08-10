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

`usage_usec` is a cumulative counter, not a utilization percentage. Other current or future flat-keyed `cpu.stat` fields are not required by v0.1 and may be ignored after the line shape is validated.

For consecutive valid samples:

```text
usage_delta_usec = current_usage_usec - previous_usage_usec
elapsed_usec = monotonic_now - previous_monotonic_time
raw_cpu_percent = usage_delta_usec / elapsed_usec * 100
```

A workload using two CPUs fully may report near 200% in raw utilization. Do not clamp collector output to 100% merely for UI convenience.

If the cumulative counter decreases or the registration identity is replaced, discard the delta and establish a new baseline.

`cpu.max` contains quota and period as two tokens. The quota token may be `max`; `max` means no limit and must remain an explicit state rather than a fabricated integer.

## Memory

Files:
- `memory.current`
- `memory.max`
- `memory.events`

`memory.current`: current memory usage in bytes.

`memory.max`: memory hard limit in bytes or literal `max`. Unlimited is represented explicitly.

`memory.events`: read-only flat-keyed cumulative event counters. The counters are hierarchical for the cgroup subtree. v0.1 records:
- `oom`: number of times an allocation was about to fail because of memory limit conditions described by the kernel interface;
- `oom_kill`: number of processes belonging to the cgroup killed by an OOM killer.

Both keys are required for a valid v0.1 `memory.events` sample. Unknown additional keys are ignored so new kernel counters do not break statd. A duplicate required key is treated as malformed input rather than guessed.

## PIDs

Files:
- `pids.current`
- `pids.max`

`pids.current`: current number accounted by the pids controller for the cgroup and its descendants according to kernel semantics.

`pids.max`: numeric hard limit or literal `max`. Unlimited is represented explicitly.

## Parser contract

Phase 1 parsers:
- consume explicit `(pointer, length)` input;
- perform no file I/O and no heap allocation;
- do not assume NUL termination;
- use unsigned 64-bit storage for kernel counters/byte values and reject decimal overflow;
- accept surrounding ASCII whitespace where kernel text interfaces commonly include a trailing newline;
- reject extra tokens for single/two-value interfaces;
- distinguish malformed input, missing required keys, and numeric range overflow.

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
