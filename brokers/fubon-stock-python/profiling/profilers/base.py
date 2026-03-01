"""Base profiler interface."""

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Any
import subprocess


@dataclass
class ProfilerResult:
    """Result from a profiler run."""

    profiler_name: str
    output_files: Dict[str, Path] = field(default_factory=dict)
    metrics: Dict[str, Any] = field(default_factory=dict)
    raw_data: Any = None


class BaseProfiler(ABC):
    """Abstract base class for all profilers."""

    name: str = "base"

    def __init__(self, output_dir: Path):
        self.output_dir = output_dir
        self.output_dir.mkdir(parents=True, exist_ok=True)

    @abstractmethod
    def check_available(self) -> bool:
        """Check if the profiling tool is available on this system."""
        pass

    @abstractmethod
    def requires_root(self) -> bool:
        """Return True if this profiler requires root privileges."""

        pass

    @abstractmethod
    def build_command(self, inner_cmd: List[str]) -> List[str]:
        """
        Wrap the inner command with profiler instrumentation.

        Args:
            inner_cmd: The command to wrap (e.g., ["python", "script.py"])

        Returns:
            The wrapped command with profiler prefix
        """

        pass

    @abstractmethod
    def parse_output(self) -> ProfilerResult:
        """
        Parse the profiler output files and return structured results.

        Returns:
            ProfilerResult containing parsed metrics and file references
        """
        pass

    def _check_tool_exists(self, tool_name: str) -> bool:
        """Check if a tool exists in PATH."""

        result = subprocess.run(["which", tool_name], capture_output=True)
        return result.returncode == 0
