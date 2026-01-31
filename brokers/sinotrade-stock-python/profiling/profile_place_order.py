"""
Profiling CLI for place_order latency analysis.

Usage:
    sudo -E ../.venv/bin/python3 profile_place_order.py --tool perf
    ../.venv/bin/python3 profile_place_order.py --tool strace
"""

import argparse
import statistics
import sys
from pathlib import Path

from profilers import BaseProfiler, StraceProfiler, PerfProfiler
from runner import ProfilingRunner
from report import ReportGenerator


SCRIPT_DIR = Path(__file__).parent
DEFAULT_TEST_SCRIPT = SCRIPT_DIR / "place_order.py"
FLAMEGRAPH_DIR = SCRIPT_DIR / "FlameGraph"


PROFILERS = ("strace", "perf")


def create_profiler(
    tool: str,
    output_dir: Path,
    python_perf_support: bool = False,
) -> BaseProfiler:
    """Create a single profiler instance."""
    if tool == "perf":
        return PerfProfiler(
            output_dir=output_dir,
            python_perf_support=python_perf_support,
        )
    elif tool == "strace":
        return StraceProfiler(output_dir=output_dir)
    else:
        raise ValueError(f"Unknown tool: {tool}")


def run_single(
    output_dir: Path,
    test_script: Path,
    tool: str,
    python_perf_support: bool = False,
    verbose: bool = False,
) -> dict:
    """Run a single profiling iteration with one tool."""

    # Create profiler
    profiler = create_profiler(tool, output_dir, python_perf_support)

    # Create runner and run
    runner = ProfilingRunner(output_dir=output_dir, profiler=profiler, verbose=verbose)
    result = runner.run(test_script)

    # Generate flamegraph if perf is used and FlameGraph tools exist
    if tool == "perf" and FLAMEGRAPH_DIR.exists():
        if verbose:
            print("  Generating flamegraph...")
        svg_path = profiler.generate_flamegraph(FLAMEGRAPH_DIR)
        if svg_path and verbose:
            print(f"  Flamegraph saved to: {svg_path}")

    # Generate reports
    if verbose:
        print("  Generating reports...")
    reporter = ReportGenerator(result)
    reporter.save_reports()

    # Print text report only in verbose mode
    if verbose:
        print()
        print(reporter.generate_text_report())

    return reporter.generate_json_report()


def main():
    parser = argparse.ArgumentParser(
        description="Profile place_order with modular profiling tools",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  sudo -E ../.venv/bin/python3 profile_place_order.py --tool perf --iterations 5

Available tools:
  strace    - Syscall tracing (network wait analysis)
  perf      - CPU sampling (library/function breakdown)
        """,
    )

    parser.add_argument(
        "--tool",
        type=str,
        required=True,
        choices=PROFILERS,
        help="Profiling tool to use",
    )
    parser.add_argument(
        "--iterations",
        type=int,
        default=5,
        help="Number of profiling iterations (default: 5)",
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default="output",
        help="Output directory (default: output)",
    )
    parser.add_argument(
        "--test-script",
        type=str,
        default=None,
        help="Path to test script (default: place_order.py)",
    )
    parser.add_argument(
        "--python-perf-support",
        action="store_true",
        help="Enable PYTHONPERFSUPPORT for Python 3.12+ symbol resolution",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Show detailed output",
    )

    args = parser.parse_args()

    # Parse test script
    test_script = Path(args.test_script) if args.test_script else DEFAULT_TEST_SCRIPT
    if not test_script.exists():
        print(f"ERROR: Test script not found: {test_script}", file=sys.stderr)
        sys.exit(1)

    # Run iterations
    results = []
    for i in range(args.iterations):
        iter_num = i + 1
        print(f"[{iter_num}/{args.iterations}] {args.tool}...", end=" ", flush=True)

        if args.verbose:
            print()  # newline for verbose output

        output_dir = Path(args.output_dir) / f"iter_{iter_num}"
        result = run_single(
            output_dir=output_dir,
            test_script=test_script,
            tool=args.tool,
            python_perf_support=args.python_perf_support,
            verbose=args.verbose,
        )
        results.append(result)

        total_ms = result["timing"]["total_ms"]
        print(f"done ({total_ms:.1f} ms)")

    # Summary statistics
    total_times = [r["timing"]["total_ms"] for r in results]
    network_times = [r["timing"]["network_ms"] for r in results]
    local_times = [r["timing"]["local_ms"] for r in results]

    print()
    summary_lines = [
        "SUMMARY",
        "=" * 40,
        f"Tool: {args.tool}",
        f"Iterations: {args.iterations}",
    ]

    if len(total_times) > 1:
        summary_lines.extend(
            [
                f"Total:   {statistics.mean(total_times):.1f} ms "
                f"(std={statistics.stdev(total_times):.1f})",
                f"Network: {statistics.mean(network_times):.1f} ms "
                f"(std={statistics.stdev(network_times):.1f})",
                f"Local:   {statistics.mean(local_times):.1f} ms "
                f"(std={statistics.stdev(local_times):.1f})",
            ]
        )
    else:
        summary_lines.append(f"Total: {total_times[0]:.1f} ms")

    summary_text = "\n".join(summary_lines)
    print(summary_text)

    # Save summary
    summary_path = Path(args.output_dir) / "SUMMARY.txt"
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(summary_text + "\n")

    print()
    print(f"Results saved to {args.output_dir}/")


if __name__ == "__main__":
    main()
