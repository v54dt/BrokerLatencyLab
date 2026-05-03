#include "latency_measurement.h"

using namespace concords_sdk::stock;
using namespace std::chrono;

void LatencyMeasurement::loadConfig(const std::string& filename) {
  try {
    config_table_ = toml::parse_file(filename);
  } catch (const toml::parse_error& err) {
    std::cerr << "ERROR: Failed to parse " << filename << ": " << err.what()
              << std::endl;
    exit(1);
  }
}

LatencyMeasurement::LatencyMeasurement() {
  loadConfig();

  std::string user_id = getConfig<std::string>("user", "user_id");
  std::string password = getConfig<std::string>("user", "password");
  std::string account = getConfig<std::string>("user", "account");
  std::string pfx_filepath = getConfig<std::string>("user", "pfx_filepath");
  std::string pfx_password = getConfig<std::string>("user", "pfx_password");

  client_ = BuildStockClient(user_id.c_str(), password.c_str(), account.c_str(),
                             pfx_filepath.c_str(), pfx_password.c_str());

  client_->SetOrderSubmitCallback([this](const OrderSubmitResult& result) {
    auto end_time = high_resolution_clock::now();

    if (result.success) {
      if (result.user_defined_id != current_order_id_) {
        std::cout << "Bypassing order: " << result.user_defined_id << std::endl;
        return;
      }
      auto duration = duration_cast<microseconds>(end_time - order_start_time_);
      measured_latency_ms_ = duration.count() / 1000.0;

      std::cout << "Order submitted successfully!" << std::endl;

      std::cout << "Round-trip latency: " << measured_latency_ms_ << " ms"
                << std::endl;

      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::cout << "Cancelling order..." << std::endl;
      client_->CancelOrder(current_order_id_);
    } else {
      std::cerr << "Order submission failed: " << result.error_message
                << std::endl;
    }

    {
      std::lock_guard<std::mutex> lock(latency_mutex_);
      order_submitted_ = true;
    }
    cv_.notify_one();
  });

  client_->SetOrderCancelCallback([this](const OrderCancelResult& result) {
    if (result.success) {
      std::cout << "Order cancelled successfully!" << std::endl;
      std::cout << "Order ID: " << result.target_id << std::endl;
    } else {
      std::cerr << "Order cancellation failed: " << result.error_message
                << std::endl;
    }

    {
      std::lock_guard<std::mutex> lock(latency_mutex_);
      order_cancelled_ = true;
    }
    cancel_cv_.notify_one();
  });
}

LatencyMeasurement::~LatencyMeasurement() {
  if (client_ && client_->IsConnected()) {
    client_->Disconnect();
  }
}

bool LatencyMeasurement::initialize() {
  if (!client_->Connect()) {
    std::cerr << "Failed to connect to broker" << std::endl;
    return false;
  }

  std::cout << "Successfully connected and authenticated with broker"
            << std::endl;
  return true;
}

void LatencyMeasurement::submitOrder() {
  std::string symbol = getConfig<std::string>("order", "symbol");
  std::string price = std::to_string(getConfig<double>("order", "price"));
  std::string quantity = std::to_string(getConfig<int>("order", "quantity"));

  order_counter_++;
  auto now = std::chrono::floor<seconds>(system_clock::now());
  std::chrono::zoned_time local{std::chrono::current_zone(), now};
  current_order_id_ =
      std::format("test-{:%Y%m%d%H%M%S}-{}", local, order_counter_);
  OrderInfo order(
      parseMarket(getConfig<std::string>("order", "market")),
      parseOrderBoard(getConfig<std::string>("order", "order_board")),
      parseFundingType(getConfig<std::string>("order", "funding_type")), symbol,
      parseSide(getConfig<std::string>("order", "side")),
      parseOrderType(getConfig<std::string>("order", "order_type")),
      parseTimeInForce(getConfig<std::string>("order", "time_in_force")),
      quantity, price,
      parseDaytradeShortSell(
          getConfig<std::string>("order", "daytrade_shortsell")),
      current_order_id_);

  {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    order_submitted_ = false;
    order_cancelled_ = false;
  }

  order_start_time_ = high_resolution_clock::now();
  client_->SubmitOrder(order);

  std::unique_lock<std::mutex> lock(latency_mutex_);
  bool submission_success =
      cv_.wait_for(lock, std::chrono::seconds(10),
                   [this] { return order_submitted_.load(); });

  if (!submission_success) {
    std::cerr << "Order submission timeout!" << std::endl;
    return;
  }

  if (order_submitted_) {
    std::cout << "Waiting for cancellation to complete..." << std::endl;
    cancel_cv_.wait_for(lock, std::chrono::seconds(10),
                        [this] { return order_cancelled_.load(); });

    if (!order_cancelled_) {
      std::cerr << "Order cancellation timeout!" << std::endl;
    } else {
      std::cout << "Order lifecycle completed (submit -> cancel)" << std::endl;
    }
  }
}

void LatencyMeasurement::runLatencyTest() {
  std::string symbol = getConfig<std::string>("order", "symbol");
  double price = getConfig<double>("order", "price");
  int quantity = getConfig<int>("order", "quantity");
  std::string side_str = getConfig<std::string>("order", "side");
  int interval = getConfig<int>("trading_hours", "interval_seconds");
  std::string start_time_str =
      getConfig<std::string>("trading_hours", "start_time");
  std::string end_time_str =
      getConfig<std::string>("trading_hours", "end_time");

  int start_time =
      std::stoi(start_time_str.substr(0, 2) + start_time_str.substr(3, 2));
  int end_time =
      std::stoi(end_time_str.substr(0, 2) + end_time_str.substr(3, 2));

  std::cout << "Trading: " << symbol << " " << side_str << " " << price << " x"
            << quantity << " every " << interval << "s during "
            << start_time_str << "-" << end_time_str << std::endl;

  // int order_count = 0;
  while (true) {
    auto now = system_clock::now();
    auto utc8_now = now + std::chrono::hours(8);
    auto utc8_dp = std::chrono::floor<std::chrono::days>(utc8_now);
    std::chrono::weekday wd{utc8_dp};
    std::chrono::hh_mm_ss hms{utc8_now - utc8_dp};

    int current_time = hms.hours().count() * 100 + hms.minutes().count();
    bool is_weekday =
        (wd != std::chrono::Saturday && wd != std::chrono::Sunday);
    bool is_trading_time =
        (current_time >= start_time && current_time <= end_time);

    if (!is_weekday || !is_trading_time) {
      std::this_thread::sleep_for(std::chrono::minutes(1));
      continue;
    }

    std::cout << "\n--- Order #" << order_counter_ << " ---" << std::endl;
    submitOrder();
    std::this_thread::sleep_for(std::chrono::seconds(interval));
  }
}
