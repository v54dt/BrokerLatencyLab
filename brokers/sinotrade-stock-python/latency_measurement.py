import shioaji as sj
import time
import requests
import toml
from datetime import datetime, timezone, timedelta
import threading
import logging
import traceback


logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)

# Timeout constants
ORDER_TIMEOUT = 10
CANCEL_TIMEOUT = 10

TAIWAN_TZ = timezone(timedelta(hours=8))


class LatencyMeasurement:
    """Measures order submission latency for Sinotrade stock trading."""

    def __init__(self, config_path="config.toml"):
        """Initialize the latency measurement system."""
        self.config = toml.load(config_path)
        self.validate_config()
        self.api = sj.Shioaji()
        self.account = None

        self.order_start_time = None
        self.measured_latency_ms = 0.0

        self.current_trade = None
        self.current_symbol = None
        self.current_action_str = None
        self.current_price = None
        self.current_quantity = None

        self.order_event = threading.Event()
        self.cancel_event = threading.Event()

        # Cache parsed constants
        self._action_map = {
            "buy": sj.constant.Action.Buy,
            "sell": sj.constant.Action.Sell,
        }
        self._price_type_map = {
            "LMT": sj.constant.StockPriceType.LMT,
            "MKT": sj.constant.StockPriceType.MKT,
        }
        self._order_type_map = {
            "ROD": sj.constant.OrderType.ROD,
            "IOC": sj.constant.OrderType.IOC,
            "FOK": sj.constant.OrderType.FOK,
        }
        self._order_lot_map = {
            "Common": sj.constant.StockOrderLot.Common,
            "IntradayOdd": sj.constant.StockOrderLot.IntradayOdd,
            "Odd": sj.constant.StockOrderLot.Odd,
            "Fixing": sj.constant.StockOrderLot.Fixing,
        }
        self._order_cond_map = {
            "Cash": sj.constant.StockOrderCond.Cash,
            "MarginTrading": sj.constant.StockOrderCond.MarginTrading,
            "ShortSelling": sj.constant.StockOrderCond.ShortSelling,
        }

        self.api.set_order_callback(self._order_callback)

    def validate_config(self):
        """Validate configuration on startup."""
        required_fields = {
            "user": ["api_key", "secret_key", "ca_cert_path", "ca_password"],
            "order": [
                "symbol",
                "price",
                "quantity",
                "action",
                "price_type",
                "order_type",
                "order_lot",
                "order_cond",
            ],
            "trading_hours": ["start_time", "end_time", "interval_seconds"],
            "api": ["url", "broker_name"],
        }

        for section, fields in required_fields.items():
            if section not in self.config:
                raise ValueError(f"Missing config section: [{section}]")
            for field in fields:
                if field not in self.config[section]:
                    raise ValueError(f"Missing required config: [{section}].{field}")

        if self.config["order"]["price"] <= 0:
            raise ValueError("Price must be positive")
        if self.config["order"]["quantity"] <= 0:
            raise ValueError("Quantity must be positive")
        if self.config["trading_hours"]["interval_seconds"] <= 0:
            raise ValueError("Interval must be positive")

        logger.info("Configuration validated successfully")

    def _order_callback(self, stat, msg):
        """Handle order state updates from exchange.

        Args:
            stat: OrderState (StockOrder, StockDeal, FuturesOrder, FuturesDeal)
            msg: dict containing order/deal information
        """
        try:
            if stat != sj.constant.OrderState.StockOrder:
                return

            operation = msg.get("operation", {})
            order = msg.get("order", {})
            op_type = operation.get("op_type", "Unknown")
            op_code = operation.get("op_code", "")
            order_id = order.get("id", "N/A")

            logger.info(
                f"Order callback: op_type={op_type}, op_code={op_code}, order_id={order_id}"
            )

            if not self.current_trade or order_id != self.current_trade.order.id:
                return

            # Handle New order (submission)
            if op_type == "New":
                self._handle_order_submitted(operation, order)

            # Handle Cancel order
            elif op_type == "Cancel":
                self._handle_order_cancelled(operation, order)

        except Exception as e:
            logger.error(f"Error in order callback: {e}")
            traceback.print_exc()

    def _handle_order_submitted(self, operation, order):
        """Handle order submission confirmation.

        Stops timing as soon as callback is triggered, then cancels order.
        """
        end_time = time.perf_counter()
        self.measured_latency_ms = (end_time - self.order_start_time) * 1000
        logger.info(f"Round-trip latency: {self.measured_latency_ms:.2f} ms")

        op_code = operation.get("op_code", "")
        op_msg = operation.get("op_msg", "")

        # "00" = success, others = fail
        if op_code != "00":
            logger.error(f"Order failed: {op_msg} (op_code: {op_code})")
            self.order_event.set()
            return

        if self.current_action_str == "buy":
            side = "B"
        else:
            side = "S"

        self.send_latency_report(
            symbol=self.current_symbol,
            side=side,
            price=self.current_price,
            volume=self.current_quantity,
            latency_ms=self.measured_latency_ms,
        )

        self.order_event.set()

        self._cancel_order()

    def _handle_order_cancelled(self, operation, order):
        """Handle order cancellation confirmation."""
        op_code = operation.get("op_code", "")
        op_msg = operation.get("op_msg", "")
        order_id = order.get("id", "N/A")

        logger.info(f"Order cancel callback: {order_id}")

        if op_code == "00":
            logger.info(f"  Order cancelled successfully")
            self.cancel_event.set()
        else:
            logger.warning(f"  Cancel failed: {op_msg}")

            self.cancel_event.set()

    def _cancel_order(self):
        try:
            if not self.current_trade:
                return

            result = self.api.cancel_order(self.current_trade)

            if result:
                logger.info(
                    f"Cancel request sent for order {self.current_trade.order.id}"
                )
            else:
                logger.warning(
                    f"Cancel order returned None for {self.current_trade.order.id}"
                )
                self.cancel_event.set()

        except Exception as e:
            logger.error(f"Error cancelling order: {e}")
            traceback.print_exc()
            self.cancel_event.set()

    def login(self):
        """Login to Shioaji API."""
        try:
            logger.info("Logging in to Sinotrade...")
            self.api.login(
                api_key=self.config["user"]["api_key"],
                secret_key=self.config["user"]["secret_key"],
                contracts_cb=lambda security_type: None,
            )

            ca_cert_path = self.config["user"]["ca_cert_path"]
            ca_password = self.config["user"]["ca_password"]
            self.api.activate_ca(
                ca_path=ca_cert_path,
                ca_passwd=ca_password,
            )
            logger.info("Login and CA activation successful")

            if not self.api.stock_account:
                logger.error("No stock account found")
                return False

            account_index = self.config["order"].get("account_index", 0)
            if isinstance(self.api.stock_account, list):
                self.account = self.api.stock_account[account_index]
            else:
                self.account = self.api.stock_account

            logger.info(f"Using account: {self.account}")
            return True

        except Exception as e:
            logger.error(f"Login failed: {e}")
            traceback.print_exc()
            return False

    def logout(self):
        """Logout from Shioaji API."""
        try:
            self.api.logout()
            logger.info("Logged out successfully")
        except Exception as e:
            logger.error(f"Logout error: {e}")

    def get_contract(self, symbol):
        """Get contract object for the given symbol."""
        try:
            contract = self.api.Contracts.Stocks.TSE.get(symbol)
            if contract:
                return contract

            contract = self.api.Contracts.Stocks.OTC.get(symbol)
            if contract:
                return contract

            logger.error(f"Contract not found for symbol: {symbol}")
            return None

        except Exception as e:
            logger.error(f"Error getting contract: {e}")
            return None

    def _parse_action(self, action):
        """Parse action string to constant."""
        action_lower = action.lower()
        if action_lower not in self._action_map:
            raise ValueError(f"Invalid action: {action}")
        return self._action_map[action_lower]

    def _parse_order_params(self):
        """Parse order parameters from config."""
        price_type_str = self.config["order"]["price_type"]
        if price_type_str not in self._price_type_map:
            raise ValueError(f"Invalid price type: {price_type_str}")

        order_type_str = self.config["order"]["order_type"]
        if order_type_str not in self._order_type_map:
            raise ValueError(f"Invalid order type: {order_type_str}")

        order_lot_str = self.config["order"]["order_lot"]
        if order_lot_str not in self._order_lot_map:
            raise ValueError(f"Invalid order lot: {order_lot_str}")

        order_cond_str = self.config["order"]["order_cond"]
        if order_cond_str not in self._order_cond_map:
            raise ValueError(f"Invalid order cond: {order_cond_str}")

        return (
            self._price_type_map[price_type_str],
            self._order_type_map[order_type_str],
            self._order_lot_map[order_lot_str],
            self._order_cond_map[order_cond_str],
        )

    def submit_order(self, symbol, action, price, quantity):
        """Submit an order and measure latency."""
        try:
            contract = self.get_contract(symbol)
            if not contract:
                logger.error(f"Cannot submit order: contract not found for {symbol}")
                return False

            action_const = self._parse_action(action)
            price_type, order_type, order_lot, order_cond = self._parse_order_params()

            order = self.api.Order(
                price=price,
                quantity=quantity,
                action=action_const,
                price_type=price_type,
                order_type=order_type,
                order_lot=order_lot,
                order_cond=order_cond,
                account=self.account,
            )

            self.order_start_time = time.perf_counter()
            logger.info(f"Submitting order: {symbol} {action} {price} x{quantity}")

            self.order_event.clear()
            self.cancel_event.clear()

            self.current_symbol = symbol
            self.current_action_str = action.lower()
            self.current_price = price
            self.current_quantity = quantity

            trade = self.api.place_order(contract, order)

            if not trade:
                logger.error("Order submission failed: No trade object returned")
                return False

            self.current_trade = trade

            # Wait for callback, but if timeout, check status manually
            if not self.order_event.wait(timeout=ORDER_TIMEOUT):
                logger.warning("No callback received, checking status manually...")

                # Update status and check if order is PreSubmitted/Submitted
                self.api.update_status(self.account)
                trades = self.api.list_trades()

                current_order = None
                for t in trades:
                    if t.order.id == trade.order.id:
                        current_order = t
                        break

                if not current_order:
                    logger.error("Order not found in trades list")
                    return False

                logger.info(f"Manual status check: {current_order.status.status}")

                # If order is PreSubmitted or Submitted, trigger callback manually
                if str(current_order.status.status) in [
                    "Status.PreSubmitted",
                    "Status.Submitted",
                ]:
                    logger.info("Order confirmed via manual status check")
                    self.current_trade = current_order

                    # Calculate latency (approximate, since callback was delayed)
                    end_time = time.perf_counter()
                    self.measured_latency_ms = (end_time - self.order_start_time) * 1000
                    logger.info(
                        f"  Round-trip latency (approx): {self.measured_latency_ms:.2f} ms"
                    )

                    # Send report and cancel
                    side = "B" if self.current_action_str == "buy" else "S"
                    self.send_latency_report(
                        symbol=self.current_symbol,
                        side=side,
                        price=self.current_price,
                        volume=self.current_quantity,
                        latency_ms=self.measured_latency_ms,
                    )

                    self._cancel_order()
                else:
                    logger.error(
                        f"Order in unexpected status: {current_order.status.status}"
                    )
                    return False

            logger.info(f"Waiting for cancel confirmation...")

            if not self.cancel_event.wait(timeout=CANCEL_TIMEOUT):
                logger.error("Order cancellation timeout")
                return False

            return True

        except Exception as e:
            logger.error(f"Error submitting order: {e}")
            traceback.print_exc()
            return False

    def send_latency_report(self, symbol, side, price, volume, latency_ms):
        """Send latency report to API."""
        try:
            api_url = self.config["api"]["url"]
            broker_name = self.config["api"]["broker_name"]

            data = {
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "broker": broker_name,
                "latency_ms": latency_ms,
                "symbol": symbol,
                "side": side,
                "price": price,
                "volume": volume,
            }

            response = requests.post(api_url, json=data, timeout=5)

            if response.status_code == 200:
                logger.info(f"Latency report sent: {latency_ms:.2f}ms for {symbol}")
            else:
                logger.warning(
                    f"Failed to send latency report: HTTP {response.status_code}"
                )

        except requests.exceptions.Timeout:
            logger.error("Timeout sending latency report")
        except requests.exceptions.RequestException as e:
            logger.error(f"Error sending latency report: {e}")
        except Exception as e:
            logger.error(f"Unexpected error sending latency report: {e}")

    def get_current_time_hhmm(self):
        """Get current time in HHMM format (Taiwan timezone UTC+8)."""
        taiwan_time = datetime.now(TAIWAN_TZ)
        return taiwan_time.hour * 100 + taiwan_time.minute

    def is_weekday(self):
        """Check if today is a weekday (Monday-Friday)."""
        taiwan_time = datetime.now(TAIWAN_TZ)
        return taiwan_time.weekday() < 5

    def run_latency_test(self):
        """Run the latency test loop."""
        symbol = self.config["order"]["symbol"]
        price = self.config["order"]["price"]
        quantity = self.config["order"]["quantity"]
        action = self.config["order"]["action"]
        interval = self.config["trading_hours"]["interval_seconds"]

        start_time_str = self.config["trading_hours"]["start_time"]
        end_time_str = self.config["trading_hours"]["end_time"]
        start_time = int(start_time_str.replace(":", ""))
        end_time = int(end_time_str.replace(":", ""))

        logger.info(f"Starting latency test")
        logger.info(f"  Symbol: {symbol}")
        logger.info(f"  Action: {action}")
        logger.info(f"  Price: {price}")
        logger.info(f"  Quantity: {quantity}")
        logger.info(f"  Interval: {interval}s")
        logger.info(f"  Trading hours: {start_time_str} - {end_time_str}")

        order_count = 0

        while True:
            try:
                current_time = self.get_current_time_hhmm()
                is_weekday = self.is_weekday()
                is_trading_time = start_time <= current_time <= end_time

                if not is_weekday or not is_trading_time:
                    if order_count == 0:
                        logger.info(
                            f"Waiting for trading hours... (current: {current_time:04d}, weekday: {is_weekday})"
                        )
                    time.sleep(60)
                    continue

                order_count += 1
                logger.info(f"\n{'='*50}")
                logger.info(f"Order #{order_count} at {current_time:04d}")
                logger.info(f"{'='*50}")

                success = self.submit_order(symbol, action, price, quantity)

                if not success:
                    logger.warning(
                        f"Order #{order_count} failed, will retry in {interval}s"
                    )

                logger.info(f"Waiting {interval}s before next order...")
                time.sleep(interval)

            except KeyboardInterrupt:
                logger.info("\nStopping latency test...")
                break
            except Exception as e:
                logger.error(f"Error in test loop: {e}")
                traceback.print_exc()
                time.sleep(interval)


def main():
    """Main entry point."""
    latency_test = LatencyMeasurement()

    try:
        if not latency_test.login():
            logger.error("Failed to login")
            return 1

        # Wait for connection to stabilize
        time.sleep(2)

        latency_test.run_latency_test()

    except KeyboardInterrupt:
        logger.info("\nShutting down...")
    except Exception as e:
        logger.error(f"Fatal error: {e}")
        traceback.print_exc()
        return 1
    finally:
        latency_test.logout()

    return 0


if __name__ == "__main__":
    exit(main())
