"""Perf profiler for CPU sampling."""

import re
import subprocess
from pathlib import Path
from typing import List, Dict, Any, Optional

from .base import BaseProfiler, ProfilerResult


class PerfProfiler(BaseProfiler):
    """Profiler using Linux perf for CPU sampling."""

    name = "perf"

    def __init__(
        self,
        output_dir: Path,
        sample_freq: int = 1000,
        call_graph: str = "dwarf",
        python_perf_support: bool = False,
    ):
        super().__init__(output_dir)
        self.sample_freq = sample_freq
        self.call_graph = call_graph
        self.python_perf_support = python_perf_support

        self.perf_data = self.output_dir / "perf.data"
        self.dso_report = self.output_dir / "perf_dso.txt"
        self.symbols_report = self.output_dir / "perf_symbols.txt"
        self.callgraph_report = self.output_dir / "perf_callgraph.txt"

    def check_available(self) -> bool:
        return self._check_tool_exists("perf")

    def requires_root(self) -> bool:
        return True

    def build_command(self, inner_cmd: List[str]) -> List[str]:
        cmd = [
            "perf",
            "record",
            "-F",
            str(self.sample_freq),
            "-g",
            "--call-graph",
            self.call_graph,
            "-o",
            str(self.perf_data),
            "--",
        ]
        cmd.extend(inner_cmd)
        return cmd

    def get_env_vars(self) -> Dict[str, str]:
        """Return environment variables needed for this profiler."""

        if self.python_perf_support:
            return {"PYTHONPERFSUPPORT": "1"}
        return {}

    def parse_output(self) -> ProfilerResult:
        if not self.perf_data.exists():
            return ProfilerResult(
                profiler_name=self.name,
                output_files={},
                metrics={"error": "perf.data not found"},
            )

        self._generate_reports()

        libs = self._parse_dso_report()
        functions = self._parse_symbol_report()
        callgraph = self._read_callgraph()

        return ProfilerResult(
            profiler_name=self.name,
            output_files={
                "perf_data": self.perf_data,
                "dso_report": self.dso_report,
                "symbols_report": self.symbols_report,
                "callgraph_report": self.callgraph_report,
            },
            metrics={
                "library_breakdown": libs,
                "top_functions": functions[:15],
                "callgraph_snippet": callgraph[:2000],
            },
        )

    def _generate_reports(self):
        """Generate text reports from perf.data."""

        # DSO (library) breakdown
        with open(self.dso_report, "w") as f:
            subprocess.run(
                [
                    "perf",
                    "report",
                    "-i",
                    str(self.perf_data),
                    "--stdio",
                    "--sort",
                    "dso",
                    "-n",
                ],
                stdout=f,
                stderr=subprocess.DEVNULL,
            )

        # Symbol (function) breakdown
        with open(self.symbols_report, "w") as f:
            subprocess.run(
                [
                    "perf",
                    "report",
                    "-i",
                    str(self.perf_data),
                    "--stdio",
                    "--sort",
                    "symbol",
                    "-n",
                ],
                stdout=f,
                stderr=subprocess.DEVNULL,
            )

        # Call graph
        with open(self.callgraph_report, "w") as f:
            subprocess.run(
                [
                    "perf",
                    "report",
                    "-i",
                    str(self.perf_data),
                    "--stdio",
                    "-g",
                    "graph",
                    "--max-stack",
                    "10",
                ],
                stdout=f,
                stderr=subprocess.DEVNULL,
            )

    def _parse_dso_report(self) -> List[Dict[str, Any]]:
        """Parse DSO (library) report."""

        libs = []
        pattern = re.compile(r"\s*([\d.]+)%\s+(\d+)\s+(.+)")

        if not self.dso_report.exists():
            return libs

        for line in self.dso_report.read_text().split("\n"):
            if line.strip().startswith("#"):
                continue

            match = pattern.match(line)
            if match:
                libs.append(
                    {
                        "overhead_pct": float(match.group(1)),
                        "samples": int(match.group(2)),
                        "library": match.group(3).strip(),
                    }
                )

        return sorted(libs, key=lambda x: x["overhead_pct"], reverse=True)

    def _parse_symbol_report(self) -> List[Dict[str, Any]]:
        """Parse symbol (function) report."""

        functions = []
        pattern = re.compile(r"\s*([\d.]+)%\s+(\d+)\s+\[.\]\s+(.+)")

        if not self.symbols_report.exists():
            return functions

        for line in self.symbols_report.read_text().split("\n"):
            if line.strip().startswith("#"):
                continue

            match = pattern.match(line)
            if match:
                functions.append(
                    {
                        "overhead_pct": float(match.group(1)),
                        "samples": int(match.group(2)),
                        "function": match.group(3).strip(),
                    }
                )

        return sorted(functions, key=lambda x: x["overhead_pct"], reverse=True)

    def _read_callgraph(self) -> str:
        """Read call graph report."""

        if not self.callgraph_report.exists():
            return ""
        return self.callgraph_report.read_text()

    def generate_flamegraph(self, flamegraph_dir: Path) -> Optional[Path]:
        """
        Generate flamegraph SVG from perf.data.

        Args:
            flamegraph_dir: Path to FlameGraph tools directory

        Returns:
            Path to generated SVG, or None if failed
        """

        stackcollapse = flamegraph_dir / "stackcollapse-perf.pl"
        flamegraph_pl = flamegraph_dir / "flamegraph.pl"

        if not stackcollapse.exists() or not flamegraph_pl.exists():
            return None

        perf_script_out = self.output_dir / "perf_script.txt"
        collapsed_stacks = self.output_dir / "perf_collapsed.txt"
        flamegraph_svg = self.output_dir / "flamegraph.svg"

        try:
            # Step 1: perf script
            with open(perf_script_out, "w") as f:
                subprocess.run(
                    ["perf", "script", "-i", str(self.perf_data)],
                    stdout=f,
                    stderr=subprocess.DEVNULL,
                    check=True,
                )

            # Step 2: stackcollapse
            with open(perf_script_out, "r") as inf, open(collapsed_stacks, "w") as outf:
                subprocess.run(
                    ["perl", str(stackcollapse)],
                    stdin=inf,
                    stdout=outf,
                    stderr=subprocess.DEVNULL,
                    check=True,
                )

            # Step 3: flamegraph
            with open(collapsed_stacks, "r") as inf, open(flamegraph_svg, "w") as outf:
                subprocess.run(
                    [
                        "perl",
                        str(flamegraph_pl),
                        "--title",
                        "place_order Performance",
                        "--width",
                        "1400",
                    ],
                    stdin=inf,
                    stdout=outf,
                    stderr=subprocess.DEVNULL,
                    check=True,
                )

            return flamegraph_svg

        except subprocess.CalledProcessError:
            return None
