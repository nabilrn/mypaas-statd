#!/usr/bin/env python3
"""Compare one-container Docker CLI stats latency with mypaas-statd snapshot latency.

This is a host benchmark harness, not a CI performance test. Run it on the same
MyPaaS host with a real running container and a healthy mypaas-statd daemon.
"""

from __future__ import annotations

import argparse
import json
import math
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
    # Allow the periodic sampler to establish a CPU delta before benchmarking.
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


def measure(operation: Callable[[], None], warmup: int, iterations: int) -> list[int]:
    for _ in range(warmup):
        operation()
    samples: list[int] = []
    for _ in range(iterations):
        start = time.perf_counter_ns()
        operation()
        samples.append(time.perf_counter_ns() - start)
    return samples


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--container", required=True, help="running Docker container name or ID")
    parser.add_argument("--runtime-id", required=True, help="statd runtime registration ID")
    parser.add_argument("--pid", type=int, required=True, help="host PID for the running container")
    parser.add_argument("--socket", default="/run/mypaas/statd.sock")
    parser.add_argument("--warmup", type=int, default=20)
    parser.add_argument("--iterations", type=int, default=200)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.pid <= 0 or args.warmup < 0 or args.iterations <= 0:
        raise SystemExit("pid/iterations must be positive and warmup non-negative")
    if not Path(args.socket).exists():
        raise SystemExit(f"statd socket not found: {args.socket}")

    ensure_registration(args.socket, args.runtime_id, args.pid)
    docker_samples = measure(lambda: docker_stats(args.container), args.warmup, args.iterations)
    statd_samples = measure(
        lambda: statd_snapshot(args.socket, args.runtime_id), args.warmup, args.iterations
    )

    result = {
        "configuration": {
            "container": args.container,
            "runtime_id": args.runtime_id,
            "warmup": args.warmup,
            "iterations": args.iterations,
        },
        "docker_cli": summarize(docker_samples),
        "statd_socket": summarize(statd_samples),
        "notes": [
            "Docker CLI measurement includes one docker process invocation per sample.",
            "statd measurement follows the production Go client model: connect + hello + snapshot per sample.",
            "Run multiple times on the same host before making performance claims.",
        ],
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
