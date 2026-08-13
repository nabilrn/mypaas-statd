# Host telemetry contract

Phase 6 extends `mypaas-statd` with small Linux-native host readers for MyPaaS dashboard telemetry. This is separate from per-runtime cgroup telemetry.

## Scope

A host snapshot may contain four independently valid sections:

- host memory capacity from `/proc/meminfo`;
- cumulative aggregate CPU counters from `/proc/stat`;
- storage capacity for the host root filesystem;
- cumulative receive/transmit byte counters for the IPv4 default-route interface.

Each section is optional. No zero value is fabricated for an unavailable source. `STATD_HOST_UNAVAILABLE` is returned only when none of the host sources can be sampled.

## Sampling and delivery

Host telemetry is sampled in the daemon's existing periodic sampling loop, currently once per second alongside runtime cgroup sampling. The IPC request path never triggers kernel/procfs collection.

When at least one host source is valid, the IPC server replaces its latest in-memory host snapshot. If a later collection attempt cannot produce any source, the daemon invalidates the host snapshot so `host_snapshot` returns `HOST_METRICS_UNAVAILABLE` instead of presenting stale counters as fresh idle telemetry.

Protocol v1 exposes the latest accepted value through:

```json
{"op":"host_snapshot"}
```

The extension remains additive. Consumers must tolerate missing sections so staged upgrades remain safe.

## Memory

Host memory is read from `/proc/meminfo` using:

- `MemTotal`;
- `MemAvailable`.

Both Linux `kB` values are converted to bytes with checked arithmetic. `MemAvailable` is intentionally used instead of `MemFree` because it is the kernel estimate of memory available for starting new applications without swapping.

Consumers may derive:

```text
used_bytes = total_bytes - available_bytes
usage_percent = used_bytes / total_bytes * 100
```

A sample is invalid if either field is missing, the unit is not `kB`, total is zero, available exceeds total, or conversion overflows.

## CPU

The daemon reads the aggregate `cpu` line from `/proc/stat` and exports cumulative counters rather than manufacturing a percentage.

The exported counters are:

- `total_ticks`: sum of user, nice, system, idle, iowait, irq, softirq, and steal counters;
- `idle_ticks`: idle + iowait.

Guest and guest_nice are not added separately because Linux already accounts them inside user/nice. Tick frequency does not need to be known for utilization ratios.

A consumer derives utilization from two successive samples:

```text
delta_total = total_ticks_2 - total_ticks_1
delta_idle  = idle_ticks_2 - idle_ticks_1
cpu_usage   = (delta_total - delta_idle) / delta_total * 100
```

If counters decrease, `delta_total` is zero, or the baseline is otherwise invalid, the consumer must reset its baseline and wait for the next sample rather than emitting a negative or fabricated percentage.

## Storage

The default storage path is `/`.

Collection uses `statvfs(3)` directly. The exported values are:

- `total_bytes = f_blocks * fragment_size`;
- `available_bytes = f_bavail * fragment_size`.

`fragment_size` uses `f_frsize`, falling back to `f_bsize` only if `f_frsize` is zero. `f_bavail` is intentionally used instead of `f_bfree` because the dashboard should show space available to ordinary workloads rather than blocks reserved for privileged use.

Phase 6 does not discover Docker/Podman data roots, sum mounts, or scan volumes. The value is explicitly root-filesystem capacity. A future dedicated storage-path feature must define its own contract instead of silently changing this meaning.

## Network

The network snapshot represents the host interface selected by the IPv4 default route.

Selection contract:

1. read bounded ASCII rows from `/proc/net/route`;
2. consider rows with destination `00000000` and the route-up flag;
3. when multiple default routes exist, select the lowest metric;
4. reject interface names that are empty, exceed the Linux interface-name bound used by this daemon, contain control/whitespace characters, or contain `/`.

After selecting the interface, read cumulative byte counters from:

- `/sys/class/net/<iface>/statistics/rx_bytes`;
- `/sys/class/net/<iface>/statistics/tx_bytes`.

These counters are cumulative. `mypaas-statd` does not manufacture bytes-per-second rates. The MyPaaS frontend may derive rates from successive samples and elapsed time, resetting its baseline if a counter decreases after an interface reset.

The IPC serializer independently rejects interface names that cannot be emitted safely by the protocol's unescaped printable-ASCII contract. An invalid interface value is represented as `network:null`, never interpolated into JSON.

This phase intentionally does not use eBPF, packet capture, traffic control, Docker APIs, or per-container network accounting.

## Failure behavior

`statd_host_sample` returns:

- `STATD_HOST_INVALID` for invalid arguments;
- `STATD_HOST_UNAVAILABLE` only when memory, CPU, storage, and network are all unavailable;
- `STATD_HOST_OK` when at least one section is valid.

The caller must inspect each section's `valid` flag. Missing telemetry is never represented as a fabricated zero.
