#!/usr/bin/env python3
"""Phase 5 process-level hardening checks for mypaas-statd."""

from __future__ import annotations

import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DAEMON = REPO_ROOT / "build" / "mypaas-statd"
RUNTIME_ID = "phase5-runtime"


def write_cgroup_fixture(path: Path, usage_usec: int = 1000) -> None:
    path.mkdir(parents=True, exist_ok=True)
    (path / "cpu.stat").write_text(f"usage_usec {usage_usec}\n", encoding="ascii")
    (path / "cpu.max").write_text("max 100000\n", encoding="ascii")
    (path / "memory.current").write_text("4096\n", encoding="ascii")
    (path / "memory.max").write_text("max\n", encoding="ascii")
    (path / "memory.events").write_text("oom 0\noom_kill 0\n", encoding="ascii")
    (path / "pids.current").write_text("3\n", encoding="ascii")
    (path / "pids.max").write_text("64\n", encoding="ascii")


def start_daemon(cgroup_root: Path, socket_path: Path) -> subprocess.Popen[str]:
    env = os.environ.copy()
    env["MYPAAS_STATD_CGROUP_ROOT"] = str(cgroup_root)
    env["MYPAAS_STATD_SOCKET"] = str(socket_path)
    proc = subprocess.Popen(
        [str(DAEMON)],
        cwd=REPO_ROOT,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            stderr = proc.stderr.read() if proc.stderr is not None else ""
            raise RuntimeError(f"daemon exited early with {proc.returncode}: {stderr}")
        if socket_path.exists():
            return proc
        time.sleep(0.02)
    stop_daemon(proc)
    raise TimeoutError("daemon did not create the Unix socket")


def stop_daemon(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
    if proc.returncode != 0:
        stderr = proc.stderr.read() if proc.stderr is not None else ""
        raise RuntimeError(f"daemon exited with {proc.returncode}: {stderr}")


def exchange(socket_path: Path, request: dict[str, object] | None = None) -> list[dict[str, object]]:
    messages = [{"op": "hello", "protocol": 1}]
    if request is not None:
        messages.append(request)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(3.0)
        client.connect(str(socket_path))
        payload = "".join(json.dumps(message, separators=(",", ":")) + "\n" for message in messages)
        client.sendall(payload.encode("utf-8"))
        reader = client.makefile("rb")
        responses = []
        for _ in messages:
            line = reader.readline()
            if not line:
                raise RuntimeError("daemon closed connection before response")
            responses.append(json.loads(line.decode("utf-8")))
        return responses


def fd_count(pid: int) -> int:
    return len(list(Path(f"/proc/{pid}/fd").iterdir()))


def main() -> int:
    if not DAEMON.exists():
        raise RuntimeError(f"daemon binary is missing: {DAEMON}")

    version = subprocess.run(
        [str(DAEMON), "--version"],
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    assert version.stdout.strip() == "mypaas-statd 0.2.0-dev", version.stdout
    assert version.stderr == "", version.stderr

    invalid = subprocess.run(
        [str(DAEMON), "--not-a-real-option"],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert invalid.returncode != 0, invalid.returncode
    assert "usage: mypaas-statd [--version]" in invalid.stderr, invalid.stderr

    tmp = Path(tempfile.mkdtemp(prefix="mypaas-statd-phase5-"))
    proc: subprocess.Popen[str] | None = None
    try:
        cgroup_root = tmp / "cgroup"
        workload = cgroup_root / "workload"
        socket_path = tmp / "statd.sock"
        write_cgroup_fixture(workload)

        proc = start_daemon(cgroup_root, socket_path)
        try:
            register = exchange(
                socket_path,
                {"op": "register", "id": RUNTIME_ID, "cgroup": "workload"},
            )[-1]
            assert register["ok"] is True, register

            snapshot = exchange(socket_path, {"op": "snapshot", "id": RUNTIME_ID})[-1]
            assert snapshot["ok"] is True, snapshot
            assert snapshot["valid"] is True, snapshot
            assert snapshot["memory"]["current_bytes"] == 4096, snapshot

            before_fds = fd_count(proc.pid)
            for index in range(200):
                write_cgroup_fixture(workload, usage_usec=1000 + index)
                response = exchange(socket_path, {"op": "snapshot", "id": RUNTIME_ID})[-1]
                assert response["ok"] is True, response
            after_fds = fd_count(proc.pid)
            assert after_fds <= before_fds + 2, (before_fds, after_fds)

            shutil.rmtree(workload)
            deadline = time.monotonic() + 4.0
            while time.monotonic() < deadline:
                status = exchange(socket_path, {"op": "status"})[-1]
                if status.get("registrations") == 0:
                    break
                time.sleep(0.2)
            else:
                raise AssertionError(f"registration was not evicted: {status}")

            hello = exchange(socket_path)[0]
            assert hello["ok"] is True, hello
        finally:
            stop_daemon(proc)
            proc = None

        write_cgroup_fixture(workload, usage_usec=5000)
        proc = start_daemon(cgroup_root, socket_path)
        status = exchange(socket_path, {"op": "status"})[-1]
        assert status["ok"] is True, status
        assert status["registrations"] == 0, status
        register = exchange(
            socket_path,
            {"op": "register", "id": RUNTIME_ID, "cgroup": "workload"},
        )[-1]
        assert register["ok"] is True, register
        snapshot = exchange(socket_path, {"op": "snapshot", "id": RUNTIME_ID})[-1]
        assert snapshot["ok"] is True, snapshot
        assert snapshot["memory"]["current_bytes"] == 4096, snapshot
    finally:
        if proc is not None:
            stop_daemon(proc)
        shutil.rmtree(tmp, ignore_errors=True)

    print("phase 5 process hardening test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
