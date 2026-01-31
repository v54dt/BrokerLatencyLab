"""
Common utilities for place_order scripts.
"""

import os
import tomllib
from pathlib import Path

import shioaji as sj
from dotenv import load_dotenv


def load_config(config_path: str | Path) -> dict:
    """Load configuration from TOML file."""

    config_path = Path(config_path)
    if not config_path.exists():
        raise SystemExit(f"Config file not found: {config_path}")
    with open(config_path, "rb") as f:
        return tomllib.load(f)


def init_api(base_dir: str | Path) -> sj.Shioaji:
    """Initialize Shioaji API, login, and activate CA."""

    base_dir = Path(base_dir)
    env_path = base_dir / ".env"
    if not env_path.exists():
        raise SystemExit(f".env file not found: {env_path}")
    load_dotenv(env_path)

    api = sj.Shioaji()

    try:
        api.login(
            api_key=os.environ["API_KEY"],
            secret_key=os.environ["SECRET_KEY"],
        )
    except Exception as e:
        raise SystemExit(f"Login failed: {e}") from e

    ca_path = os.environ["CA_CERT_PATH"]
    if not os.path.isabs(ca_path):
        ca_path = str(base_dir / ca_path)

    try:
        api.activate_ca(
            ca_path=ca_path,
            ca_passwd=os.environ["CA_PASSWORD"],
        )
    except Exception as e:
        raise SystemExit(f"CA activation failed: {e}") from e

    return api


def create_order(api: sj.Shioaji, config: dict) -> tuple:
    """Create contract and order from config."""

    contract = api.Contracts.Stocks[config["order"]["symbol"]]
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
    return contract, order
