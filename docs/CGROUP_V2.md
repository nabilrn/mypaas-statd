# cgroup v2 Metric and Resolution Contract

This document defines project-level semantics. Kernel-facing changes must be verified against authoritative Linux documentation/man-pages. If current kernel documentation conflicts with this contract, update the contract deliberately before coding.

## Roots and runtime location

Default cgroup root: `/sys/fs/cgroup`.
Default proc root: `/proc`.

A registration supplies exactly one of:
- a path relative to the configured cgroup root; or
- a positive host PID.

For PID registration, statd reads `/proc/<pid>/cgroup` from its host proc namespace. The unified cgroup-v2 membership entry is identified by hierarchy ID `0` with empty controllers (`0::$PATH`). statd strips the leading `/` and then applies the ordinary safe relative cgroup-path validation.

A missing PID/unified entry or a cgroup entry marked deleted is not converted into a guessed path. Multiple unified entries are treated as malformed. A resolved root path `/` is rejected for v0.1 runtime registrations because MyPaaS is expected to register isolated container cgroups rather than the host root cgroup.

statd never guesses Docker/systemd cgroup directory naming.

## Safe cgroup path access

The configured cgroup root is administrator configuration. Runtime relative paths:
- must not be absolute;
- must not contain empty, `.` or `..` components;
- are traversed component-by-component with no-follow directory opens;
- must not escape through symlinks.

Controller files are opened relative to the validated cgroup directory.

## CPU

v0.1 reads `cpu.stat` and `cpu.max`.

Required time counter: `usage_usec` from `cpu.stat`. It is cumulative, not a percentage. Other valid keys may be ignored.

For consecutive samples:
```text
usage_delta_usec = current_usage_usec - previous_usage_usec
elapsed_usec = monotonic_now - previous_monotonic_time
raw_cpu_percent = usage_delta_usec / elapsed_usec * 100
```

Multi-CPU workloads can exceed 100%; collector output is not clamped. First sample, identity replacement, counter regression, or invalid elapsed interval establishes a new baseline.

`cpu.max` has quota + period; quota may be literal `max`, which remains an explicit unlimited state.

## Memory

Files: `memory.current`, `memory.max`, `memory.events`.

- `memory.current`: current bytes.
- `memory.max`: hard limit bytes or `max`.
- `memory.events`: flat-keyed cumulative events. v0.1 requires `oom` and `oom_kill`; unknown additional keys are ignored and duplicate required keys are malformed.

## PIDs

Files: `pids.current`, `pids.max`.

- `pids.current`: current count according to pids-controller semantics.
- `pids.max`: numeric hard limit or `max`.

## Parser contract

Pure parsers consume explicit `(pointer,length)` data, perform no heap allocation/I/O, do not assume NUL termination, store counters as unsigned 64-bit values, reject overflow/malformed required values, and preserve `max` distinctly from numeric zero.

## Snapshot validity

Sampling failures are not zero. A snapshot records whether data exists and whether the latest sample failed. A previous good sample may be returned stale; a runtime with no successful sample reports metrics unavailable.

## Future sources

Not v0.1 unless explicitly promoted: `io.stat`, PSI files, `memory.peak`, eBPF/process events.
