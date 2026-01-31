"""Report generator for profiling results."""

import json
from pathlib import Path
from typing import Dict, Any, Optional

from runner import ProfilingResult


class ReportGenerator:
    """Generates human-readable and machine-readable reports from profiling results."""

    def __init__(self, result: ProfilingResult):
        self.result = result
        self.profiler_name = result.profiler_result.profiler_name
        self.metrics = result.profiler_result.metrics

    def generate_text_report(self) -> str:
        """Generate a human-readable text report."""
        lines = []

        # Header
        lines.append("=" * 80)
        lines.append("PROFILING REPORT")
        lines.append("=" * 80)
        lines.append("")

        # Timing Summary
        lines.append("TIMING SUMMARY")
        lines.append("-" * 80)
        total_ms = self.result.timing.total_ms

        # Get network wait from strace if available
        network_ms = 0.0
        if self.profiler_name == "strace":
            network_ms = self.metrics.get("network_wait_ms", 0.0)

        local_ms = total_ms - network_ms
        network_pct = (network_ms / total_ms * 100) if total_ms > 0 else 0
        local_pct = (local_ms / total_ms * 100) if total_ms > 0 else 0

        lines.append(f"Total Time:        {total_ms:>10.3f} ms  (100.0%)")
        lines.append(f"Network Wait:      {network_ms:>10.3f} ms  ({network_pct:>5.1f}%)")
        lines.append(f"Local Processing:  {local_ms:>10.3f} ms  ({local_pct:>5.1f}%)")
        lines.append("")

        # Profiler-specific section
        if self.profiler_name == "strace":
            lines.extend(self._format_strace_section())
        elif self.profiler_name == "perf":
            lines.extend(self._format_perf_section(local_ms))

        # Output files
        lines.append("=" * 80)
        lines.append("OUTPUT FILES")
        lines.append("=" * 80)
        lines.append(f"  {self.result.output_dir}/")

        for file_path in self.result.profiler_result.output_files.values():
            lines.append(f"    {file_path.name}")

        lines.append("")

        return "\n".join(lines)

    def _format_strace_section(self) -> list:
        """Format strace-specific report section."""
        lines = []

        lines.append("SYSCALL ANALYSIS (strace)")
        lines.append("-" * 80)
        lines.append(
            f"Syscalls:  {self.metrics.get('send_count', 0)} sends, "
            f"{self.metrics.get('recv_count', 0)} recvs, "
            f"{self.metrics.get('wait_count', 0)} waits"
        )
        lines.append("")

        # Wait events timeline
        wait_events = self.metrics.get("wait_events", [])
        if wait_events:
            lines.append("NETWORK WAIT TIMELINE")
            lines.append("-" * 80)
            lines.append(f"{'Timestamp':<15} {'Syscall':<20} {'Duration (ms)':<15}")
            lines.append("-" * 80)

            for event in wait_events[:20]:
                lines.append(
                    f"{event['timestamp']:.6f}  {event['syscall']:<20}  "
                    f"{event['duration_ms']:>12.3f}"
                )

            lines.append("")

        return lines

    def _format_perf_section(self, local_ms: float) -> list:
        """Format perf-specific report section."""
        lines = []

        if self.metrics.get("error"):
            lines.append(f"PERF: {self.metrics['error']}")
            lines.append("")
            return lines

        # Library breakdown
        libs = self.metrics.get("library_breakdown", [])
        if libs:
            lines.append("LIBRARY BREAKDOWN (perf)")
            lines.append("-" * 80)
            lines.append(f"{'Library':<50} {'CPU %':<8} {'Est. Time (ms)':<12}")
            lines.append("-" * 80)

            for lib in libs[:10]:
                lib_name = lib["library"]
                if len(lib_name) > 47:
                    lib_name = "..." + lib_name[-44:]
                est_time = (lib["overhead_pct"] / 100) * local_ms
                lines.append(
                    f"{lib_name:<50} {lib['overhead_pct']:>6.2f}%  {est_time:>10.3f}"
                )

            lines.append("")

        # Top functions
        funcs = self.metrics.get("top_functions", [])
        if funcs:
            lines.append("TOP FUNCTIONS (perf)")
            lines.append("-" * 80)
            for func in funcs[:10]:
                lines.append(f"  {func['overhead_pct']:>6.2f}%  {func['function']}")
            lines.append("")

        return lines

    def generate_json_report(self) -> Dict[str, Any]:
        """Generate machine-readable JSON report."""
        total_ms = self.result.timing.total_ms

        # Get network wait from strace
        network_ms = 0.0
        if self.profiler_name == "strace":
            network_ms = self.metrics.get("network_wait_ms", 0.0)

        local_ms = total_ms - network_ms

        return {
            "timing": {
                "total_ms": total_ms,
                "network_ms": network_ms,
                "local_ms": local_ms,
                "network_pct": (network_ms / total_ms * 100) if total_ms > 0 else 0,
                "local_pct": (local_ms / total_ms * 100) if total_ms > 0 else 0,
            },
            "profiler": {
                "name": self.profiler_name,
                "metrics": self.metrics,
                "output_files": {
                    k: str(v) for k, v in self.result.profiler_result.output_files.items()
                },
            },
        }

    def save_reports(self, output_dir: Optional[Path] = None):
        """Save both text and JSON reports to files."""
        output_dir = output_dir or self.result.output_dir

        # Text report
        text_report = self.generate_text_report()
        report_path = output_dir / "REPORT.txt"
        report_path.write_text(text_report)

        # JSON report
        json_report = self.generate_json_report()
        json_path = output_dir / "analysis.json"
        with open(json_path, "w") as f:
            json.dump(json_report, f, indent=2, default=str)

        return report_path, json_path
