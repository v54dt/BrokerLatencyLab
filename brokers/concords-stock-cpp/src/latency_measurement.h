#ifndef LATENCY_MEASUREMENT_H
#define LATENCY_MEASUREMENT_H

#include <toml++/toml.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#include "order_parsers.h"
#include "stock/include/order.h"
#include "stock/include/stock.h"

class LatencyMeasurement {
 private:
  std::unique_ptr<concords_sdk::stock::StockClient> client_;
  std::atomic<bool> order_submitted_{false};
  std::atomic<bool> order_cancelled_{false};
  std::mutex latency_mutex_;
  std::condition_variable cv_;
  std::condition_variable cancel_cv_;
  std::chrono::high_resolution_clock::time_point order_start_time_;
  double measured_latency_ms_ = 0.0;

  // Order tracking for cancellation
  int order_counter_ = 0;
  std::string current_order_id_;

  // Configuration
  toml::table config_table_;

  // Helper methods
  void loadConfig(const std::string& filename = "config.toml");

  template <typename T>
  T getConfig(const char* section, const char* key) {
    auto val = config_table_[section][key].value<T>();
    if (!val) {
      std::cerr << "ERROR: Missing config key: " << section << "." << key
                << std::endl;
      exit(1);
    }
    return *val;
  }

 public:
  LatencyMeasurement();
  ~LatencyMeasurement();

  bool initialize();
  void submitOrder();
  void runLatencyTest();
};

#endif  // LATENCY_MEASUREMENT_H
