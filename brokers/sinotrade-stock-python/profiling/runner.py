"""Profiling runner that orchestrates a profiler."""

import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from profilers.base import BaseProfiler, ProfilerResult


@dataclass
class TimingMarkers:
    """Timing markers extracted from test script output."""

    start_ns: int
    end_ns: int
    total_ns: int

    @property
    def total_ms(self) -> float:
        return self.total_ns / 1_000_000


@dataclass
class ProfilingResult:
    """Result from a profiler run."""

    timing: TimingMarkers
    profiler_result: ProfilerResult
    stderr_log: Path
    output_dir: Path


class ProfilingRunner:
    """Orchestrates a profiler to analyze a test script."""

    def __init__(
        self,
        output_dir: Path,
        profiler: BaseProfiler,
        python_cmd: Optional[str] = None,
        verbose: bool = False,
    ):
        self.output_dir = output_dir
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.profiler = profiler
        self.python_cmd = python_cmd or sys.executable
        self.stderr_log = self.output_dir / "stderr.log"
        self.verbose = verbose

    def run(self, test_script: Path) -> ProfilingResult:
        """
        Run the profiler on the test script.

        Args:
            test_script: Path to the Python test script

        Returns:
            ProfilingResult with profiler output
        """
        self._check_environment()

        cmd = self.profiler.build_command([self.python_cmd, str(test_script)])

        env = os.environ.copy()
        if hasattr(self.profiler, "get_env_vars"):
            env.update(self.profiler.get_env_vars())

        if self.verbose:
            print(f"  Python: {self.python_cmd}")
            print(f"  Command: {' '.join(cmd[:5])}...")

        result = subprocess.run(cmd, capture_output=True, text=True, env=env)

        self.stderr_log.write_text(result.stderr)

        timing = self._extract_timing_markers(result.stderr)

        if self.verbose:
            print(f"  Parsing {self.profiler.name} output...")
        profiler_result = self.profiler.parse_output()

        return ProfilingResult(
            timing=timing,
            profiler_result=profiler_result,
            stderr_log=self.stderr_log,
            output_dir=self.output_dir,
        )

    def _check_environment(self):
        """Check all prerequisites."""
        if not self.profiler.check_available():
            print(f"ERROR: {self.profiler.name} not found", file=sys.stderr)
            sys.exit(1)

        if self.profiler.requires_root() and os.geteuid() != 0:
            print(
                f"ERROR: {self.profiler.name} requires root. Run with: sudo -E python3 ...",
                file=sys.stderr,
            )
            sys.exit(1)

    def _extract_timing_markers(self, stderr: str) -> TimingMarkers:
        """Extract timing markers from stderr output."""
        start_match = re.search(r"===START=(\d+)===", stderr)
        end_match = re.search(r"===END=(\d+)===", stderr)
        total_match = re.search(r"TOTAL_NS=(\d+)", stderr)

        if not (start_match and end_match and total_match):
            # Try reading from strace log if markers are there
            if self.profiler.name == "strace":
                strace_log = self.profiler.log_file
                if strace_log.exists():
                    content = strace_log.read_text()
                    start_match = start_match or re.search(
                        r"===START=(\d+)===", content
                    )
                    end_match = end_match or re.search(r"===END=(\d+)===", content)
                    total_match = total_match or re.search(r"TOTAL_NS=(\d+)", content)

        if not (start_match and end_match and total_match):
            raise ValueError("Could not find timing markers in output")

        return TimingMarkers(
            start_ns=int(start_match.group(1)),
            end_ns=int(end_match.group(1)),
            total_ns=int(total_match.group(1)),
        )
