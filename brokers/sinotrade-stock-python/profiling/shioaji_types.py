"""
Shioaji TypedDict definitions for order callbacks.

Based on official documentation:
https://github.com/Sinotrade/Shioaji/blob/master/skills/shioaji/ORDERS.md
"""

from typing import TypedDict, Literal


class OperationDict(TypedDict):
    """Operation information in callback."""

    op_type: Literal["New", "Cancel", "UpdatePrice", "UpdateQty"]
    op_code: str  # "00" = success, others = fail
    op_msg: str


class AccountDict(TypedDict):
    """Account information."""

    account_type: Literal["S", "F"]  # Stock or Futures
    person_id: str
    broker_id: str
    account_id: str
    signed: bool


class StockOrderDict(TypedDict):
    """Stock order information."""

    id: str
    seqno: str
    ordno: str
    account: AccountDict
    action: Literal["Buy", "Sell"]
    price: float
    quantity: int
    order_type: Literal["ROD", "IOC", "FOK"]
    price_type: Literal["LMT", "MKT", "MKP"]
    order_cond: Literal["Cash", "MarginTrading", "ShortSelling"]
    order_lot: Literal["Common", "Odd", "IntradayOdd", "Fixing"]
    custom_field: str


class OrderStatusDict(TypedDict):
    """Order status information."""

    id: str
    exchange_ts: float
    modified_price: float
    cancel_quantity: int
    order_quantity: int
    web_id: str


class StockContractDict(TypedDict):
    """Stock contract information."""

    security_type: Literal["STK"]
    exchange: str
    code: str
    symbol: str
    name: str
    currency: str


class StockOrderEvent(TypedDict):
    """Stock order callback event (stat == OrderState.StockOrder)."""

    operation: OperationDict
    order: StockOrderDict
    status: OrderStatusDict
    contract: StockContractDict


class StockDealEvent(TypedDict):
    """Stock deal callback event (stat == OrderState.StockDeal)."""

    trade_id: str
    seqno: str
    ordno: str
    exchange_seq: str
    broker_id: str
    account_id: str
    action: Literal["Buy", "Sell"]
    code: str
    order_cond: Literal["Cash", "MarginTrading", "ShortSelling"]
    order_lot: Literal["Common", "Odd", "IntradayOdd", "Fixing"]
    price: float
    quantity: int
    web_id: str
    custom_field: str
    ts: float


# Futures types (for completeness)


class FuturesOrderDict(TypedDict):
    """Futures order information."""

    id: str
    seqno: str
    ordno: str
    account: AccountDict
    action: Literal["Buy", "Sell"]
    price: float
    quantity: int
    order_type: Literal["ROD", "IOC", "FOK"]
    price_type: Literal["LMT", "MKT", "MKP"]
    market_type: Literal["Day", "Night"]
    oc_type: Literal["New", "Cover", "Auto"]
    subaccount: str
    combo: bool


class FuturesContractDict(TypedDict):
    """Futures contract information."""

    security_type: Literal["FUT", "OPT"]
    code: str
    full_code: str
    exchange: str
    delivery_month: str
    delivery_date: str
    strike_price: float
    option_right: Literal["Future", "OptionCall", "OptionPut"]


class FuturesOrderEvent(TypedDict):
    """Futures order callback event (stat == OrderState.FuturesOrder)."""

    operation: OperationDict
    order: FuturesOrderDict
    status: OrderStatusDict
    contract: FuturesContractDict


class FuturesDealEvent(TypedDict):
    """Futures deal callback event (stat == OrderState.FuturesDeal)."""

    trade_id: str
    seqno: str
    ordno: str
    exchange_seq: str
    broker_id: str
    account_id: str
    action: Literal["Buy", "Sell"]
    code: str
    full_code: str
    price: float
    quantity: int
    subaccount: str
    security_type: Literal["FUT", "OPT"]
    delivery_month: str
    strike_price: float
    option_right: Literal["Future", "OptionCall", "OptionPut"]
    market_type: Literal["Day", "Night"]
    combo: bool
    ts: float
