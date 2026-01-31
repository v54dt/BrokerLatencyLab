"""
Place order and handle callback with timing markers.

Usage:
    python place_order_cb.py
    python place_order_cb.py --no-timing
    python place_order_cb.py --config /path/to/config.toml
"""

import argparse
import sys
import time
from pathlib import Path

from shioaji.constant import OrderState

from common import init_api, load_config, create_order
from shioaji_types import StockOrderEvent, StockDealEvent

BASE_DIR = Path(__file__).parent.parent
DEFAULT_CONFIG = BASE_DIR / "config.toml"


def stock_order_handler(event: StockOrderEvent):
    """Handle Stock Order Event"""
    op = event["operation"]
    order = event["order"]
    if op["op_code"] == "00":
        print(f"Stock order {op['op_type']} success: {order['id']}")
    else:
        print(f"Stock order failed: {op['op_msg']}")


def stock_deal_handler(deal: StockDealEvent):
    """Handle Stock Deal Event"""
    print(f"Stock deal: {deal['code']} @ {deal['price']} x {deal['quantity']}")


def order_cb(stat: OrderState, msg: dict):
    """Main Callback Dispatcher"""
    if stat == OrderState.StockOrder:
        stock_order_handler(msg)
    elif stat == OrderState.StockDeal:
        stock_deal_handler(msg)
    elif stat == OrderState.FuturesOrder:
        print("Futures Order")
    elif stat == OrderState.FuturesDeal:
        print("Futures Deal")
    else:
        print("Unknown Order State")


def main(enable_timing: bool = True, config_path: str | Path = None):
    """Execute place_order with callback"""
    if config_path is None:
        config_path = DEFAULT_CONFIG
    config = load_config(config_path)
    api = init_api(BASE_DIR)
    contract, order = create_order(api, config)

    # Register order callback
    api.set_order_callback(order_cb)

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

    print("Waiting for order callback...", flush=True)
    time.sleep(3)

    # Cancel order to close
    try:
        api.cancel_order(trade=trade)
    except Exception as e:
        print(f"Cancel order failed: {e}", file=sys.stderr)

    print("Waiting for cancel callback...", flush=True)
    time.sleep(3)

    return trade


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Place order and handle callback with timing markers"
    )
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
