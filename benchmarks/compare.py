#!/usr/bin/env python3
"""Compare Docker CLI metrics latency/overhead with mypaas-statd snapshots.

This is a real-host Phase 4 harness, not a CI performance benchmark. CI only
unit-tests the harness helpers. Run the actual comparison on the target MyPaaS
VM with the same workload and host load for both paths.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import resource
import socket
import statistics
import subprocess
import time
from pathlib import Path
from typing import Callable

PROTOCOL = 1
MAX_RESPONSE = 4096


def percentile(sorted_values: list[float], percentile_value: float) -> float:
    if not sorted_values:
        raise ValueError("no samples")
    rank = (len(sorted_values) - 1) * percentile_value
    lower = math.floor(rank)
    upper = math.ceil(rank)
    if lower == upper:
        return sorted_values[lower]
    weight = rank - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight


def summarize(samples_ns: list[int]) -> dict[str, float | int]:
    if not samples_ns:
        raise ValueError("no samples")
    values_ms = sorted(sample / 1_000_000.0 for sample in samples_ns)
    return {
        "samples": len(values_ms),
        "min_ms": values_ms[0],
        "mean_ms": statistics.fmean(values_ms),
        "p50_ms": percentile(values_ms, 0.50),
        "p95_ms": percentile(values_ms, 0.95),
        "p99_ms": percentile(values_ms, 0.99),
        "max_ms": values_ms[-1],
    }


def read_line(connection: socket.socket) -> dict:
    data = bytearray()
    while len(data) < MAX_RESPONSE:
        chunk = connection.recv(1)
        if not chunk:
            raise RuntimeError("statd closed connection before newline")
        if chunk == b"\n":
            return json.loads(data.decode("utf-8"))
        data.extend(chunk)
    raise RuntimeError("statd response exceeded benchmark limit")


def statd_exchange(socket_path: str, request: dict) -> dict:
    encoded = json.dumps(request, separators=(",", ":"), ensure_ascii=True).encode("ascii") + b"\n"
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2.0)
        connection.connect(socket_path)
        connection.sendall(b'{"op":"hello","protocol":1}\n')
        hello = read_line(connection)
        if not hello.get("ok") or hello.get("protocol") != PROTOCOL:
            raise RuntimeError(f"statd hello failed: {hello}")
        connection.sendall(encoded)
        response = read_line(connection)
        if not response.get("ok"):
            raise RuntimeError(f"statd request failed: {response}")
        return response


def ensure_registration(socket_path: str, runtime_id: str, pid: int) -> None:
    try:
        statd_exchange(socket_path, {"op": "unregister", "id": runtime_id})
    except RuntimeError:
        pass
    statd_exchange(socket_path, {"op": "register", "id": runtime_id, "pid": pid})
    time.sleep(1.1)


def docker_stats(container: str) -> None:
    subprocess.run(
        ["docker", "stats", "--no-stream", "--format", "{{json .}}", container],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def statd_snapshot(socket_path: str, runtime_id: str) -> None:
    response = statd_exchange(socket_path, {"op": "snapshot", "id": runtime_id})
    if not response.get("valid"):
        raise RuntimeError(f"statd returned invalid snapshot: {response}")


def warmup(operation: Callable[[], None], iterations: int) -> None:
    for _ in range(iterations):
        operation()


def measure(operation: Callable[[], None], iterations: int) -> list[int]:
    samples: list[int] = []
    for _ in range(iterations):
        start = time.perf_counter_ns()
        operation()
        samples.append(time.perf_counter_ns() - start)
    return samples


def child_cpu_seconds() -> float:
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def process_snapshot(pid: int, proc_root: Path = Path("/proc")) -> dict[str, float | int]:
    if pid <= 0:
        raise ValueError("pid must be positive")

    process_dir = proc_root / str(pid)
    stat_text = (process_dir / "stat").read_text(encoding="ascii").strip()
    close_paren = stat_text.rfind(")")
    if close_paren < 0 or close_paren + 2 >= len(stat_text):
        raise RuntimeError(f"malformed proc stat for pid {pid}")
    fields = stat_text[close_paren + 2 :].split()
    if len(fields) <= 12:
        raise RuntimeError(f"short proc stat for pid {pid}")

    ticks = os.sysconf("SC_CLK_TCK")
    if not isinstance(ticks, int) or ticks <= 0:
        raise RuntimeError("invalid SC_CLK_TCK")
    cpu_seconds = (int(fields[11]) + int(fields[12])) / ticks

    rss_bytes = 0
    voluntary = 0
    involuntary = 0
    for line in (process_dir / "status").read_text(encoding="ascii").splitlines():
        if line.startswith("VmRSS:"):
            parts = line.split()
            if len(parts) >= 2:
                rss_bytes = int(parts[1]) * 1024
        elif line.startswith("voluntary_ctxt_switches:"):
            voluntary = int(line.split(":", 1)[1].strip())
        elif line.startswith("nonvoluntary_ctxt_switches:"):
            involuntary = int(line.split(":", 1)[1].strip())

    return {
        "cpu_seconds": cpu_seconds,
        "rss_bytes": rss_bytes,
        "voluntary_context_switches": voluntary,
        "involuntary_context_switches": involuntary,
    }


def process_delta(before: dict[str, float | int], after: dict[str, float | int]) -> dict[str, float | int]:
    return {
        "cpu_seconds": max(0.0, float(after["cpu_seconds"]) - float(before["cpu_seconds"])),
        "rss_bytes_before": int(before["rss_bytes"]),
        "rss_bytes_after": int(after["rss_bytes"]),
        "voluntary_context_switches": max(
            0,
            int(after["voluntary_context_switches"]) - int(before["voluntary_context_switches"]),
        ),
        "involuntary_context_switches": max(
            0,
            int(after["involuntary_context_switches"]) - int(before["involuntary_context_switches"]),
        ),
    }


def optional_process_snapshot(pid: int | None) -> dict[str, float | int] | None:
    if pid is None:
        return None
    return process_snapshot(pid)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--container", required=True, help="running Docker container name or ID")
    parser.add_argument("--runtime-id", required=True, help="statd runtime registration ID")
    parser.add_argument("--pid", type=int, required=True, help="host PID for the running container")
    parser.add_argument("--socket", default="/run/mypaas/statd.sock")
    parser.add_argument("--statd-pid", type=int, help="mypaas-statd daemon PID for CPU/RSS deltas")
    parser.add_argument("--docker-daemon-pid", type=int, help="dockerd PID for optional CPU/RSS deltas")
    parser.add_argument("--warmup", type=int, default=20)
    parser.add_argument("--iterations", type=int, default=200)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.pid <= 0 or args.warmup < 0 or args.iterations <= 0:
        raise SystemExit("pid/iterations must be positive and warmup non-negative")
    if args.statd_pid is not None and args.statd_pid <= 0:
        raise SystemExit("statd-pid must be positive")
    if args.docker_daemon_pid is not None and args.docker_daemon_pid <= 0:
        raise SystemExit("docker-daemon-pid must be positive")
    if not Path(args.socket).exists():
        raise SystemExit(f"statd socket not found: {args.socket}")

    ensure_registration(args.socket, args.runtime_id, args.pid)

    warmup(lambda: docker_stats(args.container), args.warmup)
    docker_children_before = child_cpu_seconds()
    dockerd_before = optional_process_snapshot(args.docker_daemon_pid)
    docker_started = time.perf_counter_ns()
    docker_samples = measure(lambda: docker_stats(args.container), args.iterations)
    docker_elapsed_ns = time.perf_counter_ns() - docker_started
    docker_children_cpu = max(0.0, child_cpu_seconds() - docker_children_before)
    dockerd_after = optional_process_snapshot(args.docker_daemon_pid)

    warmup(lambda: statd_snapshot(args.socket, args.runtime_id), args.warmup)
    statd_before = optional_process_snapshot(args.statd_pid)
    statd_started = time.perf_counter_ns()
    statd_samples = measure(
        lambda: statd_snapshot(args.socket, args.runtime_id), args.iterations
    )
    statd_elapsed_ns = time.perf_counter_ns() - statd_started
    statd_after = optional_process_snapshot(args.statd_pid)

    docker_result: dict[str, object] = {
        "latency": summarize(docker_samples),
        "wall_seconds": docker_elapsed_ns / 1_000_000_000.0,
        "cli_child_cpu_seconds": docker_children_cpu,
        "measured_process_spawns": args.iterations,
    }
    if dockerd_before is not None and dockerd_after is not None:
        docker_result["docker_daemon"] = process_delta(dockerd_before, dockerd_after)

    statd_result: dict[str, object] = {
        "latency": summarize(statd_samples),
        "wall_seconds": statd_elapsed_ns / 1_000_000_000.0,
        "client_process_spawns": 0,
    }
    if statd_before is not None and statd_after is not None:
        statd_result["daemon"] = process_delta(statd_before, statd_after)

    result = {
        "configuration": {
            "container": args.container,
            "runtime_id": args.runtime_id,
            "warmup": args.warmup,
            "iterations": args.iterations,
            "statd_pid": args.statd_pid,
            "docker_daemon_pid": args.docker_daemon_pid,
        },
        "docker_cli": docker_result,
        "statd_socket": statd_result,
        "notes": [
            "Recorded Docker samples spawn one docker CLI process each; warmup spawns are excluded from measured_process_spawns.",
            "cli_child_cpu_seconds is CPU consumed by benchmark child processes, not dockerd itself.",
            "Pass --docker-daemon-pid to measure dockerd CPU/RSS/context-switch deltas.",
            "Pass --statd-pid to measure mypaas-statd CPU/RSS/context-switch deltas.",
            "statd latency follows the production client model: connect + hello + snapshot per sample.",
            "Protocol v1 does not expose sampler timestamp, so metric-age/freshness is not fabricated by this harness.",
            "Run repeated trials on the same target host before making performance claims.",
        ],
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
