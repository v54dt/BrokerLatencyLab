/**
 * Submit order with timing markers.
 *
 * Usage:
 *     ./submit_order                                    # Default config
 *     ./submit_order --no-timing                        # Disable timing markers
 *     ./submit_order --config config.toml               # Custom config file
 */

#include <toml++/toml.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

#include "order_parsers.h"
#include "stock/include/stock.h"

using namespace concords_sdk::stock;

template <typename T>
T get_config(const toml::table& config, const char* section, const char* key) {
  auto val = config[section][key].value<T>();
  if (!val) {
    std::cerr << "ERROR: Missing config key: " << section << "." << key
              << std::endl;
    exit(1);
  }
  return *val;
}

int main(int argc, char* argv[]) {
  bool enable_timing = true;
  const char* config_path = "config.toml";

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--no-timing") == 0) {
      enable_timing = false;
    } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      config_path = argv[++i];
    }
  }

  toml::table config;
  try {
    config = toml::parse_file(config_path);
  } catch (const toml::parse_error& err) {
    std::cerr << "ERROR: Failed to parse " << config_path << ": "
              << err.description() << std::endl;
    return 1;
  }

  // Load credentials
  std::string user_id = get_config<std::string>(config, "user", "user_id");
  std::string password = get_config<std::string>(config, "user", "password");
  std::string account = get_config<std::string>(config, "user", "account");
  std::string pfx_filepath =
      get_config<std::string>(config, "user", "pfx_filepath");
  std::string pfx_password =
      get_config<std::string>(config, "user", "pfx_password");

  auto client =
      BuildStockClient(user_id.c_str(), password.c_str(), account.c_str(),
                       pfx_filepath.c_str(), pfx_password.c_str());

  // Load order parameters
  std::string symbol = get_config<std::string>(config, "order", "symbol");
  std::string price =
      std::to_string(get_config<double>(config, "order", "price"));
  std::string quantity =
      std::to_string(get_config<int>(config, "order", "quantity"));
  Market market =
      parseMarket(get_config<std::string>(config, "order", "market"));
  OrderBoard order_board =
      parseOrderBoard(get_config<std::string>(config, "order", "order_board"));
  FundingType funding_type = parseFundingType(
      get_config<std::string>(config, "order", "funding_type"));
  Side side = parseSide(get_config<std::string>(config, "order", "side"));
  OrderType order_type =
      parseOrderType(get_config<std::string>(config, "order", "order_type"));
  TimeInForce time_in_force = parseTimeInForce(
      get_config<std::string>(config, "order", "time_in_force"));
  DaytradeShortSell daytrade_shortsell = parseDaytradeShortSell(
      get_config<std::string>(config, "order", "daytrade_shortsell"));

  auto now = std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
  std::chrono::zoned_time local{std::chrono::current_zone(), now};
  std::string user_defined_id = std::format("profile-{:%Y%m%d%H%M%S}", local);

  OrderInfo order_info(market, order_board, funding_type, symbol, side,
                       order_type, time_in_force, quantity, price,
                       daytrade_shortsell, user_defined_id);

  std::atomic<bool> order_submitted{false};
  std::atomic<bool> order_cancelled{false};
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
      std::cerr << "User defined ID: " << result.user_defined_id << std::endl;
      std::cerr << "Transaction ID: " << result.transaction_id << std::endl;

      std::this_thread::sleep_for(std::chrono::seconds(1));
      client->CancelOrder(result.user_defined_id);
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
