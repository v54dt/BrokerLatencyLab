import os
import time
import requests
import toml
import logging
import traceback
from datetime import datetime, timezone, timedelta

import fubon_neo
import fubon_neo.constant
from fubon_neo.sdk import FubonSDK, Order

logging.basicConfig(level=logging.INFO, format="%(message)s")
logger = logging.getLogger(__name__)

TAIWAN_TZ = timezone(timedelta(hours=8))


class LatencyMeasurement:
    """Measures order submission latency for Fubon stock trading."""

    def __init__(self, config_path="config.toml"):
        self.config = toml.load(config_path)
        self.validate_config()

        self.sdk = FubonSDK()
        self.account = None
        self.measured_latency_ms = 0.0

    def validate_config(self):
        required = {
            "user": ["user_id", "password", "pfx_filepath", "pfx_password"],
            "order": [
                "symbol",
                "price",
                "quantity",
                "action",
                "market_type",
                "price_type",
                "time_in_force",
                "order_type",
            ],
            "trading_hours": ["start_time", "end_time", "interval_seconds"],
            "api": ["url", "broker_name"],
        }
        for section, fields in required.items():
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

    def login(self):
        try:
            user = self.config["user"]
            resp = self.sdk.login(
                user["user_id"],
                user["password"],
                user["pfx_filepath"],
                user["pfx_password"],
            )
            if not resp.is_success or not resp.data:
                logger.error(
                    f"Failed to connect to broker: {resp.message or '(no message)'}"
                )
                return False

            self.account = resp.data[0]
            logger.info("Successfully connected and authenticated with broker")
            return True

        except Exception as e:
            logger.error(f"Failed to connect to broker: {e}")
            traceback.print_exc()
            return False

    def submit_order(self, symbol, action, price, quantity):
        try:
            order = Order(
                buy_sell=getattr(fubon_neo.constant.BSAction, action),
                symbol=symbol,
                price=str(price),
                quantity=quantity,
                market_type=getattr(
                    fubon_neo.constant.MarketType,
                    self.config["order"]["market_type"],
                ),
                price_type=getattr(
                    fubon_neo.constant.PriceType,
                    self.config["order"]["price_type"],
                ),
                time_in_force=getattr(
                    fubon_neo.constant.TimeInForce,
                    self.config["order"]["time_in_force"],
                ),
                order_type=getattr(
                    fubon_neo.constant.OrderType,
                    self.config["order"]["order_type"],
                ),
            )

            t1 = time.perf_counter()
            resp = self.sdk.stock.place_order(self.account, order)
            t2 = time.perf_counter()

            if not resp.is_success or resp.data is None:
                logger.error(
                    f"Order submission failed: {resp.message or '(no message)'}"
                )
                return

            self.measured_latency_ms = (t2 - t1) * 1000

            logger.info("Order submitted successfully!")
            logger.info(f"Round-trip latency: {self.measured_latency_ms:.2f} ms")

            side = "B" if action.lower() == "buy" else "S"
            self.send_latency_report(
                symbol=symbol,
                side=side,
                price=price,
                volume=quantity,
                latency_ms=self.measured_latency_ms,
            )

            time.sleep(1)

            logger.info("Cancelling order...")
            cancel_resp = self.sdk.stock.cancel_order(self.account, resp.data)

            if cancel_resp.is_success:
                order_no = cancel_resp.data.order_no if cancel_resp.data else "(none)"
                logger.info("Order cancelled successfully!")
                logger.info(f"Order ID: {order_no}")
                logger.info("Order lifecycle completed (submit -> cancel)")
            else:
                logger.error(
                    f"Order cancellation failed: {cancel_resp.message or '(no message)'}"
                )

        except Exception as e:
            logger.error(f"Order submission failed: {e}")
            traceback.print_exc()

    def send_latency_report(self, symbol, side, price, volume, latency_ms):
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

            if not (200 <= response.status_code < 300):
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
        taiwan_time = datetime.now(TAIWAN_TZ)
        return taiwan_time.hour * 100 + taiwan_time.minute

    def is_weekday(self):
        return datetime.now(TAIWAN_TZ).weekday() < 5

    def run_latency_test(self):
        symbol = self.config["order"]["symbol"]
        price = self.config["order"]["price"]
        quantity = self.config["order"]["quantity"]
        action = self.config["order"]["action"]
        interval = self.config["trading_hours"]["interval_seconds"]

        start_time_str = self.config["trading_hours"]["start_time"]
        end_time_str = self.config["trading_hours"]["end_time"]
        start_time = int(start_time_str.replace(":", ""))
        end_time = int(end_time_str.replace(":", ""))

        side = "B" if action.lower() == "buy" else "S"
        logger.info(
            f"Trading: {symbol} {side} {price} x{quantity} every {interval}s "
            f"during {start_time_str}-{end_time_str}"
        )

        order_count = 0

        while True:
            try:
                current_time = self.get_current_time_hhmm()
                is_trading_time = start_time <= current_time <= end_time

                if not self.is_weekday() or not is_trading_time:
                    time.sleep(60)
                    continue

                order_count += 1
                logger.info(f"\n--- Order #{order_count} ---")

                self.submit_order(symbol, action, price, quantity)
                time.sleep(interval)

            except KeyboardInterrupt:
                break
            except Exception as e:
                logger.error(f"Error in test loop: {e}")
                traceback.print_exc()
                time.sleep(interval)


def main():
    latency_test = LatencyMeasurement()

    if not latency_test.login():
        return 1

    time.sleep(2)

    try:
        latency_test.run_latency_test()
    except KeyboardInterrupt:
        pass
    except Exception as e:
        logger.error(f"Fatal error: {e}")
        traceback.print_exc()
        return 1

    return 0


if __name__ == "__main__":
    rc = main()
    os._exit(rc)
