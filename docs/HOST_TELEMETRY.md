# Host telemetry contract

Phase 6 extends `mypaas-statd` with small Linux-native host readers for MyPaaS host telemetry and delivery diagnosis. This is separate from per-runtime cgroup telemetry.

The goal is not to turn statd into a general monitoring agent. The host readers expose bounded cumulative kernel counters that let an operator correlate a load window with host/network behavior without packet capture, eBPF, traffic control, Docker/Podman APIs, or a second daemon.

## Scope

A host sample may contain independently valid sections for:

- host memory capacity from `/proc/meminfo`;
- cumulative aggregate CPU counters from `/proc/stat`;
- storage capacity for the host root filesystem;
- interface counters for the IPv4 default-route interface;
- TCP counters from `/proc/net/snmp` and `TcpExt` counters from `/proc/net/netstat`;
- UDP counters from `/proc/net/snmp`.

Each source is optional. No zero value is fabricated for an unavailable source. `STATD_HOST_UNAVAILABLE` is returned only when none of the host sources can be sampled.

## Sampling and delivery

Host telemetry is sampled in the daemon's existing periodic sampling loop, currently once per second alongside runtime cgroup sampling. The IPC request path never triggers kernel/procfs collection.

Protocol v1 continues to expose the existing compatible host snapshot contract through:

```json
{"op":"host_snapshot"}
```

The first delivery-diagnostics slice intentionally does not expand that IPC payload. The richer network/TCP/UDP counters are available to operators and benchmark automation through the read-only CLI:

```bash
mypaas-statd --delivery-snapshot
```

The command performs one bounded host sample and prints one JSON object. It does not modify routes, firewall rules, sysctls, queue disciplines, sockets, Caddy, cloudflared, or container state.

For benchmark diagnosis, capture one snapshot immediately before the load window and one immediately after it, then compare cumulative counter deltas. Do not infer a bottleneck from a single absolute counter value that may include traffic from before the experiment.

## Memory

Host memory is read from `/proc/meminfo` using `MemTotal` and `MemAvailable`. Both Linux `kB` values are converted to bytes with checked arithmetic. `MemAvailable` is intentionally used instead of `MemFree` because it is the kernel estimate of memory available for starting new applications without swapping.

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
- `idle_ticks`: idle + iowait;
- `iowait_ticks`;
- `irq_ticks`;
- `softirq_ticks`;
- `steal_ticks`.

Guest and guest_nice are not added separately because Linux already accounts them inside user/nice. Tick frequency does not need to be known for utilization ratios.

A consumer derives utilization from two successive samples:

```text
delta_total = total_ticks_2 - total_ticks_1
delta_idle  = idle_ticks_2 - idle_ticks_1
cpu_usage   = (delta_total - delta_idle) / delta_total * 100
```

If counters decrease, `delta_total` is zero, or the baseline is otherwise invalid, the consumer must reset its baseline and wait for the next sample rather than emitting a negative or fabricated percentage. IRQ/softirq deltas are kept separately so delivery diagnosis can detect kernel network-processing work hidden by low application CPU.

## Storage

The default storage path is `/`.

Collection uses `statvfs(3)` directly. The exported values are:

- `total_bytes = f_blocks * fragment_size`;
- `available_bytes = f_bavail * fragment_size`.

`fragment_size` uses `f_frsize`, falling back to `f_bsize` only if `f_frsize` is zero. `f_bavail` is intentionally used instead of `f_bfree` because the dashboard should show space available to ordinary workloads rather than blocks reserved for privileged use.

Phase 6 does not discover Docker/Podman data roots, sum mounts, or scan volumes.

## Default-route interface

The network snapshot represents the host interface selected by the IPv4 default route.

Selection contract:

1. read bounded ASCII rows from `/proc/net/route`;
2. consider rows with destination `00000000` and the route-up flag;
3. when multiple default routes exist, select the lowest metric;
4. reject unsafe or overlong interface names.

After selecting the interface, statd reads cumulative counters from `/sys/class/net/<iface>/statistics/`:

- `rx_bytes`, `tx_bytes`;
- `rx_packets`, `tx_packets`;
- `rx_errors`, `tx_errors`;
- `rx_dropped`, `tx_dropped`;
- `rx_missed_errors`.

`rx_missed_errors` is also collected because Linux documents it as packets missed by the host, commonly indicating that the host/interface could not keep up with receive packet rate.

These are Linux interface statistics, not application throughput. Byte and packet rates are derived from successive samples. A rise in error/drop counters during the exact load window is evidence of a local interface/host-side delivery problem; absence of such a rise does not prove that the external network path is healthy.

The network section is valid only when all required interface counters can be read. This avoids presenting a partial interface sample as if missing counters were zero.

## TCP delivery counters

TCP collection has two independently valid sub-sources.

### `/proc/net/snmp` (`Tcp`)

The diagnostic snapshot records cumulative:

- `CurrEstab`;
- `InSegs`;
- `OutSegs`;
- `RetransSegs`;
- `InErrs`;
- `OutRsts`;
- `AttemptFails`;
- `EstabResets`.

### `/proc/net/netstat` (`TcpExt`)

The diagnostic snapshot records cumulative:

- `TCPSynRetrans`;
- `ListenOverflows`;
- `ListenDrops`;
- `TCPAbortOnMemory`;
- `TCPAbortOnTimeout`;
- `TCPOrigDataSent`.

`ListenOverflows` and `ListenDrops` are useful evidence for listener/accept-queue pressure. `TCPAbortOnMemory` and `TCPAbortOnTimeout` help distinguish socket-memory or timeout failure from ordinary application CPU saturation. Retransmission counters are evidence of transport retry/loss behavior, but non-zero retransmissions are not automatically a bottleneck.

`TCPOrigDataSent` is kept because Linux documents it as original outgoing data excluding retransmission and pure ACK behavior, making it a more useful denominator than total output segments when examining retransmission rate.

The parser requests explicit key names from the kernel table and ignores unrelated fields. This is required because `/proc/net/snmp` contains fields such as `MaxConn` whose value may be signed and which are outside this unsigned diagnostic contract.

If a required key in one TCP source is missing or malformed, that TCP sub-source is invalid rather than partially fabricated. The other TCP sub-source remains independently usable.

## UDP delivery counters

UDP collection reads cumulative counters from the `Udp` section of `/proc/net/snmp`:

- `InDatagrams`;
- `OutDatagrams`;
- `InErrors`;
- `NoPorts`;
- `RcvbufErrors`;
- `SndbufErrors`.

The receive/send buffer error counters are particularly useful when a UDP-based delivery component such as a QUIC tunnel is active. They are host-wide UDP evidence, not cloudflared-specific attribution. If the kernel does not expose the complete requested UDP key set, the UDP section is invalid and statd does not manufacture zeroes.

## Interpreting a benchmark window

Use deltas over the same load window. Examples of evidence, not automatic verdicts:

```text
interface drops/errors/rx_missed_errors increase
    -> local interface/host-side packet delivery pressure is plausible

softirq delta consumes a large share of total CPU delta while user CPU stays low
    -> kernel network processing may be consuming a CPU path even when app CPU is low

ListenOverflows/ListenDrops increase
    -> local TCP listener accept queue pressure is proven

TCPAbortOnMemory increases
    -> local TCP memory pressure participated in failed connections

RetransSegs / TCPSynRetrans / TCPAbortOnTimeout increase sharply
    -> transport retry/loss/timeout behavior participated in the window

UDP RcvbufErrors/SndbufErrors increase
    -> local UDP socket-buffer pressure participated in the window

application/host compute idle + no local drop/queue/buffer-error signal
    -> evidence shifts away from local compute/kernel saturation and toward
       external delivery path, transfer shape, RTT/peering, or the load runner
```

Statd deliberately does not classify a bottleneck or apply magic thresholds. Correlation and workload context remain the responsibility of the benchmark/diagnostic layer.

## Failure behavior

`statd_host_sample` returns:

- `STATD_HOST_INVALID` for invalid arguments;
- `STATD_HOST_UNAVAILABLE` only when memory, CPU, storage, interface, TCP, and UDP sources are all unavailable;
- `STATD_HOST_OK` when at least one section is valid.

The caller must inspect each section's validity flags. Missing telemetry is never represented as a fabricated zero.

## Explicit non-goals

This phase still does not implement:

- eBPF;
- packet capture;
- traffic control or sysctl tuning;
- netlink-based per-container accounting;
- Docker/Podman API calls from statd;
- per-project network attribution;
- time-series persistence;
- automatic remediation or autoscaling decisions.
