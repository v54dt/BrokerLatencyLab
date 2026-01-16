/**
 * Place order with timing markers.
 *
 * Usage:
 *     ./place_order                              # Uses order_config.toml
 *     ./place_order --no-timing                  # Disable timing markers
 *     ./place_order --config order_config.toml   # Use custom config file
 */

#include <toml++/toml.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#include "stock-client/order.h"
#include "stock-client/stock_client.h"

using namespace concordsapi::stockclient;

Market parseMarket(const std::string& str) {
  if (str == "TSE") return Market::TSE;
  if (str == "OTC") return Market::OTC;
  std::cerr << "Invalid market: " << str << std::endl;
  exit(1);
}

OrderBoard parseOrderBoard(const std::string& str) {
  if (str == "RoundLot") return OrderBoard::RoundLot;
  if (str == "OddLot") return OrderBoard::OddLot;
  if (str == "PostMarket_Fixed") return OrderBoard::PostMarket_Fixed;
  if (str == "PostMarket_OddLot") return OrderBoard::PostMarket_OddLot;
  std::cerr << "Invalid order_board: " << str << std::endl;
  exit(1);
}

FundingType parseFundingType(const std::string& str) {
  if (str == "Cash") return FundingType::Cash;
  if (str == "MarginBuy") return FundingType::MarginBuy;
  if (str == "MarginShortSell") return FundingType::MarginShortSell;
  std::cerr << "Invalid funding_type: " << str << std::endl;
  exit(1);
}

Side parseSide(const std::string& str) {
  if (str == "Buy" || str == "B") return Side::Buy;
  if (str == "Sell" || str == "S") return Side::Sell;
  std::cerr << "Invalid side: " << str << std::endl;
  exit(1);
}

OrderType parseOrderType(const std::string& str) {
  if (str == "Limit") return OrderType::Limit;
  if (str == "Market") return OrderType::Market;
  std::cerr << "Invalid order_type: " << str << std::endl;
  exit(1);
}

TimeInForce parseTimeInForce(const std::string& str) {
  if (str == "ROD") return TimeInForce::ROD;
  if (str == "IOC") return TimeInForce::IOC;
  if (str == "FOK") return TimeInForce::FOK;
  std::cerr << "Invalid time_in_force: " << str << std::endl;
  exit(1);
}

DaytradeShortSell parseDaytradeShortSell(const std::string& str) {
  if (str == "True" || str == "Y") return DaytradeShortSell::True;
  if (str == "False" || str == "N") return DaytradeShortSell::False;
  std::cerr << "Invalid daytrade_shortsell: " << str << std::endl;
  exit(1);
}

std::string find_config_file(const char* arg_config) {
  if (arg_config) {
    return arg_config;
  }

  return "order_config.toml";
}

int main(int argc, char* argv[]) {
  bool enable_timing = true;
  const char* config_path = nullptr;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--no-timing") == 0) {
      enable_timing = false;
    } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      config_path = argv[i + 1];
      i++;
    }
  }

  std::string config_file = find_config_file(config_path);
  auto config = toml::parse_file(config_file);

  std::string user_id = *config["user"]["user_id"].value<std::string>();
  std::string password = *config["user"]["password"].value<std::string>();
  std::string account = *config["user"]["account"].value<std::string>();
  std::string pfx_filepath =
      *config["user"]["pfx_filepath"].value<std::string>();
  std::string pfx_password =
      *config["user"]["pfx_password"].value<std::string>();

  auto client =
      BuildStockClient(user_id.c_str(), password.c_str(), account.c_str(),
                       pfx_filepath.c_str(), pfx_password.c_str());

  std::string symbol = *config["order"]["symbol"].value<std::string>();
  std::string price = *config["order"]["price"].value<std::string>();
  std::string quantity = *config["order"]["quantity"].value<std::string>();
  Market market = parseMarket(*config["order"]["market"].value<std::string>());
  OrderBoard order_board =
      parseOrderBoard(*config["order"]["order_board"].value<std::string>());
  FundingType funding_type =
      parseFundingType(*config["order"]["funding_type"].value<std::string>());
  Side side = parseSide(*config["order"]["side"].value<std::string>());
  OrderType order_type =
      parseOrderType(*config["order"]["order_type"].value<std::string>());
  TimeInForce time_in_force =
      parseTimeInForce(*config["order"]["time_in_force"].value<std::string>());
  DaytradeShortSell daytrade_shortsell = parseDaytradeShortSell(
      *config["order"]["daytrade_shortsell"].value<std::string>());

  OrderInfo order_info(market, order_board, funding_type, symbol, side,
                       order_type, time_in_force, quantity, price,
                       daytrade_shortsell);

  std::atomic<bool> order_submitted{false};
  std::atomic<bool> order_cancelled{false};
  std::string current_order_id;
  std::string current_order_ticket_id;
  std::mutex mtx;
  std::condition_variable cv, cancel_cv;
  uint64_t submit_end_ns = 0;
  uint64_t start_ns = 0;

  client->SetOrderSubmitCallback([&](const OrderSubmitResult& result) {
    if (enable_timing) {
      auto end = std::chrono::high_resolution_clock::now();
      submit_end_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          end.time_since_epoch())
                          .count();

      std::cerr << "===END=" << submit_end_ns << "===" << std::endl;
      std::cerr << "TOTAL_NS=" << (submit_end_ns - start_ns) << std::endl;
    }

    if (result.success) {
      std::cerr << "Order submitted successfully!" << std::endl;
      std::cerr << "Order ID: " << result.order_id << std::endl;
      std::cerr << "Order Ticket ID: " << result.order_ticket_id << std::endl;

      current_order_id = result.order_id;
      current_order_ticket_id = result.order_ticket_id;

      std::this_thread::sleep_for(std::chrono::seconds(1));
      client->CancelOrder(current_order_id, current_order_ticket_id,
                          order_info);
    } else {
      std::cerr << "Order submission failed: " << result.error_message
                << std::endl;
    }

    {
      std::lock_guard<std::mutex> lock(mtx);
      order_submitted = true;
    }
    cv.notify_one();
  });

  client->SetOrderCancelCallback([&](const OrderCancelResult& result) {
    if (result.success) {
      std::cerr << "Order cancelled successfully!" << std::endl;
    } else {
      std::cerr << "Order cancellation failed: " << result.error_message
                << std::endl;
    }

    {
      std::lock_guard<std::mutex> lock(mtx);
      order_cancelled = true;
    }
    cancel_cv.notify_one();
  });

  if (!client->Connect()) {
    std::cerr << "Failed to connect" << std::endl;
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  if (!client->Login()) {
    std::cerr << "Failed to login" << std::endl;
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::cerr << "Connected and logged in" << std::endl;

  if (enable_timing) {
    auto start = std::chrono::high_resolution_clock::now();
    start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   start.time_since_epoch())
                   .count();
    std::cerr << "===START=" << start_ns << "===" << std::endl;
  }

  client->SubmitOrder(order_info);

  {
    std::unique_lock<std::mutex> lock(mtx);
    bool success = cv.wait_for(lock, std::chrono::seconds(10),
                               [&] { return order_submitted.load(); });
    if (!success) {
      std::cerr << "Order submission timeout" << std::endl;
      return 1;
    }
  }

  {
    std::unique_lock<std::mutex> lock(mtx);
    bool success = cancel_cv.wait_for(lock, std::chrono::seconds(10),
                                      [&] { return order_cancelled.load(); });
    if (!success) {
      std::cerr << "Order cancellation timeout" << std::endl;
      return 1;
    }
  }

  if (client->IsConnected()) {
    client->Disconnect();
  }

  return 0;
}
