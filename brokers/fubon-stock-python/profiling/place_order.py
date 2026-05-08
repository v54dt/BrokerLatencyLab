"""
Submit order with timing markers.

Usage:
    python place_order.py                        # Default config
    python place_order.py --no-timing            # Disable timing markers
    python place_order.py --config config.toml   # Custom config file
"""

import argparse
import sys
import time
import tomllib
from pathlib import Path

import fubon_neo
import fubon_neo.constant
from fubon_neo.sdk import FubonSDK, Order

SCRIPT_DIR = Path(__file__).parent
DEFAULT_CONFIG = SCRIPT_DIR / "config.toml"


def load_config(config_path: str | Path) -> dict:
    config_path = Path(config_path)
    if not config_path.exists():
        print(f"ERROR: Config file not found: {config_path}", file=sys.stderr)
        sys.exit(1)
    with open(config_path, "rb") as f:
        return tomllib.load(f)


def cancel_order(sdk, order_no, account):
    orders = sdk.stock.get_order_results(account)

    target_order = None
    for order in orders.data:
        if order.order_no == order_no:
            target_order = order

    if target_order is None:
        print("ERROR: Order not found for cancellation", file=sys.stderr)
        return None

    return sdk.stock.cancel_order(account, target_order)


def main(enable_timing: bool = True, config_path: str | Path = None):
    if config_path is None:
        config_path = DEFAULT_CONFIG

    config = load_config(config_path)

    sdk = FubonSDK()
    accounts = sdk.login(
        config["user"]["user_id"],
        config["user"]["password"],
        config["user"]["pfx_filepath"],
        config["user"]["pfx_password"],
    )

    if not accounts.is_success:
        print(f"ERROR: Login failed: {accounts.message}", file=sys.stderr)
        return

    account = accounts.data[0]

    order = Order(
        buy_sell=getattr(fubon_neo.constant.BSAction, config["order"]["action"]),
        symbol=config["order"]["symbol"],
        price=str(config["order"]["price"]),
        quantity=config["order"]["quantity"],
        market_type=getattr(
            fubon_neo.constant.MarketType, config["order"]["market_type"]
        ),
        price_type=getattr(fubon_neo.constant.PriceType, config["order"]["price_type"]),
        time_in_force=getattr(
            fubon_neo.constant.TimeInForce, config["order"]["time_in_force"]
        ),
        order_type=getattr(fubon_neo.constant.OrderType, config["order"]["order_type"]),
    )

    if enable_timing:
        start_ns = time.perf_counter_ns()
        print(f"===START={start_ns}===", file=sys.stderr, flush=True)

    order_response = sdk.stock.place_order(account, order)

    if enable_timing:
        end_ns = time.perf_counter_ns()
        print(f"===END={end_ns}===", file=sys.stderr, flush=True)
        print(f"TOTAL_NS={end_ns - start_ns}", file=sys.stderr, flush=True)

    if not order_response.is_success:
        print(f"ERROR: Place order failed: {order_response.message}", file=sys.stderr)
        return

    time.sleep(1)
    cancel_order(sdk, order_response.data.order_no, account)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Place order with timing markers")
    parser.add_argument(
        "--no-timing", action="store_true", help="Disable timing markers"
    )
    parser.add_argument(
        "--config",
        type=str,
        default=None,
        help="Path to config (default: config.toml)",
    )
    args = parser.parse_args()
    main(enable_timing=not args.no_timing, config_path=args.config)
