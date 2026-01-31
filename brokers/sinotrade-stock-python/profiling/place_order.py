"""
Place order with timing markers.

Usage:
    python place_order.py
    python place_order.py --no-timing
    python place_order.py --config /path/to/config.toml
"""

import argparse
import sys
import time
from pathlib import Path

from common import init_api, load_config, create_order

BASE_DIR = Path(__file__).parent.parent
DEFAULT_CONFIG = BASE_DIR / "config.toml"


def main(enable_timing: bool = True, config_path: str | Path = None):
    """Execute place_order."""
    if config_path is None:
        config_path = DEFAULT_CONFIG
    config = load_config(config_path)
    api = init_api(BASE_DIR)
    contract, order = create_order(api, config)

    # Place order to open
    if enable_timing:
        start_ns = time.perf_counter_ns()
        print(f"===START={start_ns}===", file=sys.stderr, flush=True)

    try:
        trade = api.place_order(contract, order)
    except Exception as e:
        print(f"Place order failed: {e}", file=sys.stderr)
        return None

    if enable_timing:
        end_ns = time.perf_counter_ns()
        print(f"===END={end_ns}===", file=sys.stderr, flush=True)
        print(f"TOTAL_NS={end_ns - start_ns}", file=sys.stderr, flush=True)

    # Cancel order to close
    try:
        api.cancel_order(trade=trade)
    except Exception as e:
        print(f"Cancel order failed: {e}", file=sys.stderr)

    return trade


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Place order with timing markers")
    parser.add_argument(
        "--no-timing", action="store_true", help="Disable timing markers"
    )
    parser.add_argument(
        "--config",
        type=str,
        default=None,
        help="Path to order config (default: ../config.toml)",
    )
    args = parser.parse_args()
    main(enable_timing=not args.no_timing, config_path=args.config)
