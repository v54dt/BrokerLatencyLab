import fubon_neo
import time
import sys
import os
import argparse
import tomllib
from pathlib import Path

from fubon_neo.sdk import FubonSDK, Order
from dotenv import load_dotenv

BASE_DIR = Path(__file__).parent.parent
DEFAULT_CONFIG = BASE_DIR / "config.toml"


def load_config(config_path: str | Path) -> dict:
    """Load configuration from TOML file."""

    config_path = Path(config_path)
    if not config_path.exists():
        raise SystemExit(f"Config file not found: {config_path}")
    with open(config_path, "rb") as f:
        return tomllib.load(f)


def del_order(sdk, order_no, account):

    target_order = None
    orders = sdk.stock.get_order_results(account)

    for order in orders.data:
        if order.order_no == order_no:
            target_order = order

    if target_order is None:
        return None
    else:
        response = sdk.stock.cancel_order(account, target_order)
        return response


def main(enable_timing: bool = True, config_path: str | Path = None):
    """Execute place_order"""

    if config_path is None:
        config_path = DEFAULT_CONFIG

    config = load_config(config_path)

    load_dotenv()

    sdk = FubonSDK()
    accounts = sdk.login(
        os.environ["USER_ID"],
        os.environ["USER_PASSWORD"],
        os.environ["CA_CERT_PATH"],
        os.environ["CA_PASSWORD"],
    )

    if not accounts.is_success:
        print(f"Login failed: {accounts.message}")
        return

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
        user_def=None,
    )

    if enable_timing:
        start_ns = time.perf_counter_ns()
        print(f"===START={start_ns}===", file=sys.stderr, flush=True)
    order_response = sdk.stock.place_order(accounts.data[0], order)

    if enable_timing:
        end_ns = time.perf_counter_ns()
        print(f"===END={end_ns}===", file=sys.stderr, flush=True)
        print(f"TOTAL_NS={end_ns - start_ns}", file=sys.stderr, flush=True)

    response = del_order(sdk, order_response.data.order_no, accounts.data[0])

    if response is None:
        print("Cancel order failed")


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
