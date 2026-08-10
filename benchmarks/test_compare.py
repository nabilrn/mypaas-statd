from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from benchmarks.compare import percentile, process_delta, process_snapshot, summarize


class BenchmarkHelperTests(unittest.TestCase):
    def test_percentile_and_summary(self) -> None:
        values = [1.0, 2.0, 3.0, 4.0]
        self.assertEqual(percentile(values, 0.50), 2.5)
        summary = summarize([1_000_000, 2_000_000, 3_000_000])
        self.assertEqual(summary["samples"], 3)
        self.assertEqual(summary["p50_ms"], 2.0)
        self.assertEqual(summary["min_ms"], 1.0)
        self.assertEqual(summary["max_ms"], 3.0)

    def test_empty_summary_rejected(self) -> None:
        with self.assertRaises(ValueError):
            summarize([])

    def test_process_snapshot_handles_spaces_in_comm(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            proc_root = Path(temp_dir)
            process_dir = proc_root / "4321"
            process_dir.mkdir()
            # After the closing parenthesis: state (field 3) through stime (field 15).
            (process_dir / "stat").write_text(
                "4321 (mypaas statd worker) S 1 2 3 4 5 6 7 8 9 10 25 15 0 0\n",
                encoding="ascii",
            )
            (process_dir / "status").write_text(
                "Name:\tmypaas-statd\n"
                "VmRSS:\t128 kB\n"
                "voluntary_ctxt_switches:\t30\n"
                "nonvoluntary_ctxt_switches:\t7\n",
                encoding="ascii",
            )
            with mock.patch("benchmarks.compare.os.sysconf", return_value=100):
                snapshot = process_snapshot(4321, proc_root)

        self.assertEqual(snapshot["cpu_seconds"], 0.4)
        self.assertEqual(snapshot["rss_bytes"], 128 * 1024)
        self.assertEqual(snapshot["voluntary_context_switches"], 30)
        self.assertEqual(snapshot["involuntary_context_switches"], 7)

    def test_process_snapshot_rejects_short_stat(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            proc_root = Path(temp_dir)
            process_dir = proc_root / "5"
            process_dir.mkdir()
            (process_dir / "stat").write_text("5 (x) S 1\n", encoding="ascii")
            (process_dir / "status").write_text("VmRSS:\t1 kB\n", encoding="ascii")
            with self.assertRaises(RuntimeError):
                process_snapshot(5, proc_root)

    def test_process_delta_is_non_negative(self) -> None:
        before = {
            "cpu_seconds": 2.0,
            "rss_bytes": 100,
            "voluntary_context_switches": 9,
            "involuntary_context_switches": 4,
        }
        after = {
            "cpu_seconds": 3.5,
            "rss_bytes": 120,
            "voluntary_context_switches": 12,
            "involuntary_context_switches": 6,
        }
        delta = process_delta(before, after)
        self.assertEqual(delta["cpu_seconds"], 1.5)
        self.assertEqual(delta["rss_bytes_before"], 100)
        self.assertEqual(delta["rss_bytes_after"], 120)
        self.assertEqual(delta["voluntary_context_switches"], 3)
        self.assertEqual(delta["involuntary_context_switches"], 2)


if __name__ == "__main__":
    unittest.main()
