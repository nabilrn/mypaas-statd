# Host telemetry contract

Phase 6 extends `mypaas-statd` with small Linux-native host-capacity readers for MyPaaS dashboard telemetry. This is separate from per-runtime cgroup telemetry.

## Scope

The host snapshot contract contains two independently valid sections:

- storage capacity for the host root filesystem;
- cumulative receive/transmit byte counters for the IPv4 default-route interface.

If one source is unavailable, the other may still be returned as valid. No zero value is fabricated for an unavailable source.

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

These counters are cumulative. `mypaas-statd` does not manufacture bytes-per-second rates. The MyPaaS control plane or frontend may derive rates from successive samples and elapsed time, resetting its baseline if a counter decreases after an interface reset.

This phase intentionally does not use eBPF, packet capture, traffic control, Docker APIs, or per-container network accounting.

## Kernel/interface references

Implementation semantics were checked against:

- Linux `statvfs(3)` / Linux man-pages for mounted-filesystem statistics;
- Linux kernel interface-statistics documentation and `sysfs-class-net-statistics` ABI for `rx_bytes` and `tx_bytes`;
- Linux procfs networking documentation for `/proc/net` namespace behavior.

The `/proc/net/route` row subset consumed by MyPaaS is locked by deterministic parser fixtures and must remain bounded. If a future kernel/runtime target requires a different route-discovery mechanism, prefer an explicit contract change over heuristic parsing.

## Failure behavior

`statd_host_sample` returns:

- `STATD_HOST_INVALID` for invalid arguments;
- `STATD_HOST_UNAVAILABLE` only when both storage and network sources are unavailable;
- `STATD_HOST_OK` when at least one section is valid.

The caller must inspect each section's `valid` flag. Missing telemetry is not represented as a fabricated zero.
