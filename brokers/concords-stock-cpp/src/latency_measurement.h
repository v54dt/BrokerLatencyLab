#ifndef LATENCY_MEASUREMENT_H
#define LATENCY_MEASUREMENT_H

#include <toml++/toml.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "network.h"
#include "order_parsers.h"
#include "stock/include/order.h"
#include "stock/include/stock.h"

class AsyncPoster;  // defined in latency_measurement.cpp

// Resolved config values, populated once at construction.
struct CachedConfig {
  // [user]
  std::string user_id, password, account, pfx_filepath, pfx_password;

  // [order]
  std::string symbol;
  std::string price_str;
  std::string quantity_str;
  std::string side_str;
  double price = 0.0;
  int quantity = 0;
  concords_sdk::stock::Market market{};
  concords_sdk::stock::OrderBoard order_board{};
  concords_sdk::stock::FundingType funding_type{};
  concords_sdk::stock::Side side{};
  concords_sdk::stock::OrderType order_type{};
  concords_sdk::stock::TimeInForce time_in_force{};
  concords_sdk::stock::DaytradeShortSell daytrade_shortsell{};

  // [api]
  std::string api_url;
  std::string broker_name;

  // [trading_hours]
  int interval_seconds = 0;
  int trading_start_hhmm = 0;
  int trading_end_hhmm = 0;
  std::string trading_start_str;
  std::string trading_end_str;

  // [network_probe]
  std::string probe_host;
  int probe_port = 0;
};

class LatencyMeasurement {
 private:
  std::unique_ptr<concords_sdk::stock::StockClient> client_;
  std::atomic<bool> order_submitted_{false};
  std::atomic<bool> order_cancelled_{false};
  std::mutex latency_mutex_;
  std::condition_variable cv_;
  std::condition_variable cancel_cv_;

  // Stage timestamps for one iteration
  std::chrono::high_resolution_clock::time_point order_start_time_;
  std::chrono::high_resolution_clock::time_point submit_call_end_time_;
  std::chrono::high_resolution_clock::time_point ack_time_;
  std::chrono::high_resolution_clock::time_point cancel_call_time_;
  std::chrono::high_resolution_clock::time_point cancel_done_time_;

  // Per-iteration measurements
  std::optional<double> total_ms_;
  std::optional<double> sdk_local_ms_;
  std::optional<double> ack_rtt_ms_;
  std::optional<double> cancel_rtt_ms_;

  // Outcome state
  std::string outcome_;  // "success" | "ack_timeout" | "submit_error" |
                         // "cancel_timeout" | "cancel_error"
  std::string error_message_;

  // Kernel TCP info
  TcpInfoSnapshot tcp_info_;

  // Order tracking
  int order_counter_ = 0;
  std::string current_order_id_;

  CachedConfig config_;
  std::unique_ptr<NetworkProbe> network_probe_;
  std::unique_ptr<AsyncPoster> poster_;

  void loadConfig(const std::string& filename = "config.toml");
  void resetIterationState();
  void sendOrderReport();
  void sendNetworkReport(const NetworkProbeResult& result);

 public:
  LatencyMeasurement();
  ~LatencyMeasurement();

  bool initialize();
  void submitOrder();
  void runNetworkProbe();
  void runLatencyTest();
};

#endif  // LATENCY_MEASUREMENT_H
