"""
Place order with timing markers.

Usage:
    python place_order.py
    python place_order.py --no-timing
    python place_order.py --config order_config.toml
"""

import shioaji as sj
import time
import sys
import os
import argparse
import tomllib
from dotenv import load_dotenv
from pathlib import Path


def main(enable_timing: bool = True, config_path: str = None):
    """Execute place_order."""
    load_dotenv()

    if config_path is None:
        config_path = Path(__file__).parent / "order_config.toml"
    else:
        config_path = Path(config_path)

    with open(config_path, "rb") as f:
        config = tomllib.load(f)

    # Initialize shioaji sdk and Login
    api = sj.Shioaji()
    api.login(
        api_key=os.environ["API_KEY"],
        secret_key=os.environ["SECRET_KEY"],
    )

    ca_path = os.environ["CA_CERT_PATH"]
    if not os.path.isabs(ca_path):
        parent_dir = Path(__file__).parent.parent
        ca_path = str(parent_dir / ca_path)

    api.activate_ca(
        ca_path=ca_path,
        ca_passwd=os.environ["CA_PASSWORD"],
    )

    contract = api.Contracts.Stocks[config["contract"]["stock_id"]]
    order = api.Order(
        price=config["order"]["price"],
        quantity=config["order"]["quantity"],
        action=getattr(sj.constant.Action, config["order"]["action"]),
        price_type=getattr(sj.constant.StockPriceType, config["order"]["price_type"]),
        order_type=getattr(sj.constant.OrderType, config["order"]["order_type"]),
        order_lot=getattr(sj.constant.StockOrderLot, config["order"]["order_lot"]),
        order_cond=getattr(sj.constant.StockOrderCond, config["order"]["order_cond"]),
        account=api.stock_account,
    )

    # Place order to open
    if enable_timing:
        start_ns = time.perf_counter_ns()
        print(f"===START={start_ns}===", file=sys.stderr, flush=True)

    trade = api.place_order(contract, order)

    if enable_timing:
        end_ns = time.perf_counter_ns()
        print(f"===END={end_ns}===", file=sys.stderr, flush=True)
        print(f"TOTAL_NS={end_ns - start_ns}", file=sys.stderr, flush=True)

    # Cancel order to close
    api.cancel_order(trade=trade)

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
        help="Path to order config (default: order_config.toml)",
    )
    args = parser.parse_args()
    main(enable_timing=not args.no_timing, config_path=args.config)
