#ifndef LATENCY_MEASUREMENT_H
#define LATENCY_MEASUREMENT_H

#include <toml++/toml.h>

#include <chrono>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "fubon.hpp"
#include "order_parsers.h"
#include "sdk.hpp"

class LatencyMeasurement {
 private:
  // The Fubon SDK requires a callback to be registered. We don't rely on
  // callbacks for latency timing (place_order returns synchronously) so this
  // is a no-op stub.
  struct NoOpCallback : public fubon::Callback {
    void on_order(std::optional<std::string>,
                  const fubon::OrderResult&) override {}
    void on_order_changed(std::optional<std::string>,
                          const fubon::OrderResult&) override {}
    void on_filled(std::optional<std::string>,
                   const fubon::FilledData&) override {}
    void on_futopt_order(std::optional<std::string>,
                         const fubon::FutOptOrderResult&) override {}
    void on_futopt_order_changed(std::optional<std::string>,
                                 const fubon::FutOptOrderResult&) override {}
    void on_futopt_filled(std::optional<std::string>,
                          const fubon::FutOptFilledData&) override {}
    void on_event(const std::string&, const std::string&) override {}
  };

  std::unique_ptr<fubon::FubonSDK> sdk_;
  std::shared_ptr<NoOpCallback> callback_;
  fubon::Account account_;
  bool logged_in_ = false;

  toml::table config_table_;
  int order_counter_ = 0;
  double measured_latency_ms_ = 0.0;

  void loadConfig(const std::string& filename = "config.toml");
  void sendLatencyReport();

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
