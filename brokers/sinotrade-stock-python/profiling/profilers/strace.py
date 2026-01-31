"""Strace profiler for syscall tracing."""

import re
from dataclasses import dataclass
from pathlib import Path
from typing import List, Dict, Any, Optional

from .base import BaseProfiler, ProfilerResult


@dataclass
class SyscallEvent:
    """Parsed syscall event from strace output."""

    timestamp: float  # Seconds since midnight
    pid: int
    syscall_name: str
    duration: float  # In seconds
    return_value: str
    raw_line: str


class StraceProfiler(BaseProfiler):
    """Profiler using strace for syscall tracing."""

    name = "strace"

    # Default syscalls to trace for network latency analysis
    DEFAULT_TRACE_FILTER = (
        "sendto,send,sendmsg,recvfrom,recv,recvmsg," "epoll_wait,epoll_pwait,poll,write"
    )

    # Syscalls that represent blocking network wait
    WAIT_SYSCALLS = ["epoll_wait", "epoll_pwait", "poll", "select", "pselect6"]

    def __init__(
        self,
        output_dir: Path,
        trace_filter: Optional[str] = None,
        follow_forks: bool = True,
    ):
        super().__init__(output_dir)
        self.trace_filter = trace_filter or self.DEFAULT_TRACE_FILTER
        self.follow_forks = follow_forks
        self.log_file = self.output_dir / "strace.log"

    def check_available(self) -> bool:
        return self._check_tool_exists("strace")

    def requires_root(self) -> bool:
        return False

    def build_command(self, inner_cmd: List[str]) -> List[str]:
        cmd = [
            "strace",
            "-T",  # Show time spent in each syscall
            "-tt",  # Absolute timestamps with microseconds
        ]

        if self.follow_forks:
            cmd.append("-f")  # Follow threads/forks

        cmd.extend(
            [
                "-o",
                str(self.log_file),
                "-e",
                f"trace={self.trace_filter}",
            ]
        )

        cmd.extend(inner_cmd)
        return cmd

    def parse_output(self) -> ProfilerResult:
        if not self.log_file.exists():
            return ProfilerResult(
                profiler_name=self.name,
                output_files={},
                metrics={"error": "strace log not found"},
            )

        events = self._parse_log()
        metrics = self._analyze_events(events)

        return ProfilerResult(
            profiler_name=self.name,
            output_files={"strace_log": self.log_file},
            metrics=metrics,
            raw_data=events,
        )

    def _parse_log(self) -> List[SyscallEvent]:
        """Parse strace log file into structured events."""

        events = []

        # Pattern 1: [pid 123] HH:MM:SS.ffffff syscall(...) = ret <duration>
        pattern1 = re.compile(
            r"\[pid\s+(\d+)\]\s+(\d{2}:\d{2}:\d{2}\.\d+)\s+(.+?)\s*=\s*(.+?)\s+<([\d.]+)>"
        )
        # Pattern 2: 123 HH:MM:SS.ffffff syscall(...) = ret <duration>
        pattern2 = re.compile(
            r"^(\d+)\s+(\d{2}:\d{2}:\d{2}\.\d+)\s+(.+?)\s*=\s*(.+?)\s+<([\d.]+)>"
        )

        with open(self.log_file) as f:
            for line in f:
                match = pattern1.search(line) or pattern2.match(line)
                if match:
                    pid = int(match.group(1))
                    timestamp_str = match.group(2)
                    syscall = match.group(3)
                    ret_val = match.group(4)
                    duration = float(match.group(5))

                    # Convert HH:MM:SS.ffffff to seconds since midnight
                    h, m, s = timestamp_str.split(":")
                    timestamp = int(h) * 3600 + int(m) * 60 + float(s)

                    syscall_name = syscall.split("(")[0] if "(" in syscall else syscall

                    events.append(
                        SyscallEvent(
                            timestamp=timestamp,
                            pid=pid,
                            syscall_name=syscall_name,
                            duration=duration,
                            return_value=ret_val,
                            raw_line=line.strip(),
                        )
                    )

        return events

    def _analyze_events(self, events: List[SyscallEvent]) -> Dict[str, Any]:
        """Analyze parsed events to compute metrics."""

        # Categorize events
        sends = [e for e in events if "send" in e.syscall_name]
        recvs = [e for e in events if "recv" in e.syscall_name]
        waits = [e for e in events if e.syscall_name in self.WAIT_SYSCALLS]

        # Calculate network wait time
        network_wait_ms = sum(e.duration * 1000 for e in waits)

        return {
            "network_wait_ms": network_wait_ms,
            "send_count": len(sends),
            "recv_count": len(recvs),
            "wait_count": len(waits),
            "total_events": len(events),
            "wait_events": [
                {
                    "timestamp": e.timestamp,
                    "syscall": e.syscall_name,
                    "duration_ms": e.duration * 1000,
                }
                for e in waits
            ],
        }
