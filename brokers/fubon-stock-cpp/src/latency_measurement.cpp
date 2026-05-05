#include "latency_measurement.h"

#include <curl/curl.h>

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
  sdk_ = std::make_unique<fubon::FubonSDK>();
  callback_ = std::make_shared<NoOpCallback>();
  sdk_->register_callback(callback_);
}

LatencyMeasurement::~LatencyMeasurement() {
  if (sdk_ && logged_in_) {
    sdk_->logout();
  }
}

bool LatencyMeasurement::initialize() {
  auto user_id = getConfig<std::string>("user", "user_id");
  auto password = getConfig<std::string>("user", "password");
  auto cert_path = getConfig<std::string>("user", "pfx_filepath");
  auto cert_pass = getConfig<std::string>("user", "pfx_password");

  auto resp = sdk_->login(user_id, password, cert_path, cert_pass);
  if (!resp.is_success) {
    std::cerr << "Failed to connect to broker: "
              << resp.message.value_or("(no message)") << std::endl;
    return false;
  }
  if (!resp.data.has_value() || resp.data.value().empty()) {
    std::cerr << "Failed to connect to broker: no accounts" << std::endl;
    return false;
  }

  account_ = resp.data.value()[0];
  logged_in_ = true;

  std::cout << "Successfully connected and authenticated with broker"
            << std::endl;
  return true;
}

void LatencyMeasurement::submitOrder() {
  using namespace fubon_parsers;

  order_counter_++;
  auto now = std::chrono::floor<seconds>(system_clock::now());
  std::chrono::zoned_time local{std::chrono::current_zone(), now};
  std::string order_id =
      std::format("test-{:%Y%m%d%H%M%S}-{}", local, order_counter_);

  fubon::Order order{
      parseSide(getConfig<std::string>("order", "side")),
      getConfig<std::string>("order", "symbol"),
      std::format("{:g}", getConfig<double>("order", "price")),
      getConfig<int64_t>("order", "quantity"),
      parseMarketType(getConfig<std::string>("order", "market_type")),
      parsePriceType(getConfig<std::string>("order", "price_type")),
      parseTimeInForce(getConfig<std::string>("order", "time_in_force")),
      parseOrderType(getConfig<std::string>("order", "order_type")),
      order_id,
  };

  auto t1 = high_resolution_clock::now();
  auto resp = sdk_->stock->place_order(account_, order, std::nullopt);
  auto t2 = high_resolution_clock::now();

  if (!resp.is_success || !resp.data.has_value()) {
    std::cerr << "Order submission failed: "
              << resp.message.value_or("(no message)") << std::endl;
    return;
  }

  measured_latency_ms_ =
      duration_cast<microseconds>(t2 - t1).count() / 1000.0;

  std::cout << "Order submitted successfully!" << std::endl;
  std::cout << "Round-trip latency: " << measured_latency_ms_ << " ms"
            << std::endl;

  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::cout << "Cancelling order..." << std::endl;
  auto cancel_resp =
      sdk_->stock->cancel_order(account_, resp.data.value(), std::nullopt);

  if (cancel_resp.is_success) {
    std::cout << "Order cancelled successfully!" << std::endl;
    std::cout << "Order ID: "
              << resp.data.value().order_no.value_or("(none)") << std::endl;
    std::cout << "Order lifecycle completed (submit -> cancel)" << std::endl;
    sendLatencyReport();
  } else {
    std::cerr << "Order cancellation failed: "
              << cancel_resp.message.value_or("(no message)") << std::endl;
  }
}

void LatencyMeasurement::sendLatencyReport() {
  std::string api_url = getConfig<std::string>("api", "url");
  std::string broker = getConfig<std::string>("api", "broker_name");
  std::string symbol = getConfig<std::string>("order", "symbol");
  std::string side_raw = getConfig<std::string>("order", "side");
  std::string side =
      (!side_raw.empty() && (side_raw[0] == 'B' || side_raw[0] == 'b')) ? "B"
                                                                       : "S";
  double price = getConfig<double>("order", "price");
  int64_t volume = getConfig<int64_t>("order", "quantity");

  auto now_s = std::chrono::floor<seconds>(system_clock::now());
  std::string timestamp = std::format("{:%Y-%m-%dT%H:%M:%SZ}", now_s);

  std::string body = std::format(
      R"({{"timestamp":"{}","broker":"{}","latency_ms":{:.3f},)"
      R"("symbol":"{}","side":"{}","price":{},"volume":{}}})",
      timestamp, broker, measured_latency_ms_, symbol, side, price, volume);

  CURL* curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Failed to send latency report: curl_easy_init failed"
              << std::endl;
    return;
  }

  struct curl_slist* headers =
      curl_slist_append(nullptr, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(
      curl, CURLOPT_WRITEFUNCTION,
      +[](char*, size_t s, size_t n, void*) -> size_t { return s * n; });

  CURLcode res = curl_easy_perform(curl);
  long status_code = 0;
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK || status_code < 200 || status_code >= 300) {
    std::cerr << "Failed to send latency report: " << curl_easy_strerror(res)
              << " (HTTP " << status_code << ")" << std::endl;
  }
}

void LatencyMeasurement::runLatencyTest() {
  std::string symbol = getConfig<std::string>("order", "symbol");
  double price = getConfig<double>("order", "price");
  int64_t quantity = getConfig<int64_t>("order", "quantity");
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

    std::cout << "\n--- Order #" << order_counter_ + 1 << " ---" << std::endl;
    submitOrder();
    std::this_thread::sleep_for(std::chrono::seconds(interval));
  }
}
