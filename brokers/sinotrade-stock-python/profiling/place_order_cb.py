"""
Place order and handle callback with timing markets.

Usage:
    python place_order_cb.py
    python place_order_cb.py --no-timing
    python place_order_cb.py --config order_config.toml
"""

import shioaji as sj
import time
import sys
import os
import argparse
import tomllib
from dotenv import load_dotenv
from pathlib import Path
from shioaji.constant import OrderState
from shioaji_types import StockOrderEvent, StockDealEvent


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
        print(f"Futures Order")
    elif stat == OrderState.FuturesDeal:
        print(f"Futures Deal")
    else:
        print(f"Unknown Order State")


def main(enable_timing: bool = True, config_path: str = None):
    """Execute place_order with callback"""
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

    # Register order callback
    api.set_order_callback(order_cb)

    # Place order to open
    if enable_timing:
        start_ns = time.perf_counter_ns()
        print(f"===START={start_ns}===", file=sys.stderr, flush=True)

    trade = api.place_order(contract, order)

    if enable_timing:
        end_ns = time.perf_counter_ns()
        print(f"===END={end_ns}===", file=sys.stderr, flush=True)
        print(f"TOTAL_NS={end_ns - start_ns}", file=sys.stderr, flush=True)

    print("Waiting for order callback...", flush=True)
    time.sleep(3)

    # Cancel order to close
    api.cancel_order(trade=trade)

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
        help="Path to order config (default: order_config.toml)",
    )
    args = parser.parse_args()
    main(enable_timing=not args.no_timing, config_path=args.config)
