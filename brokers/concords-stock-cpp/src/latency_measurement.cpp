#include "latency_measurement.h"

#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace chrono = std::chrono;

// ---------- Tunables ----------

namespace {

constexpr chrono::seconds kCallbackTimeout{10};  // wait for ack / cancel cb
constexpr chrono::seconds kCancelDelay{1};       // hold order before cancel
constexpr long kHttpTimeoutSec = 5;              // metric POST timeout

constexpr chrono::minutes kMarketClosedRecheckInterval{1};  // re-check cadence when market is closed

}  // namespace

// ---------- Logging ----------

namespace {

std::ostream& ts(std::ostream& os) {
  auto now = chrono::floor<chrono::seconds>(chrono::system_clock::now());
  chrono::zoned_time local{chrono::current_zone(), now};
  return os << std::format("[{:%Y-%m-%d %H:%M:%S}] ", local);
}

}  // namespace

// ---------- JSON helpers ----------

namespace {

std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string fmtOpt(const std::optional<double>& v) {
  return v.has_value() ? std::format("{:.3f}", *v) : std::string("null");
}

std::string fmtOpt(const std::optional<uint32_t>& v) {
  return v.has_value() ? std::format("{}", *v) : std::string("null");
}

std::string fmtOpt(const std::optional<bool>& v) {
  if (!v.has_value()) return "null";
  return *v ? "true" : "false";
}

std::string fmtOptString(const std::string& s) {
  if (s.empty()) return "null";
  return "\"" + jsonEscape(s) + "\"";
}

std::string nowTimestamp() {
  auto now_s = chrono::floor<chrono::seconds>(chrono::system_clock::now());
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now_s);
}

}  // namespace

// ---------- Config parsing helpers ----------

namespace {

[[noreturn]] void missingConfig(const char* section, const char* key) {
  throw std::runtime_error(
      std::format("Missing config key: {}.{}", section, key));
}

template <typename T>
T required(const toml::table& t, const char* section, const char* key) {
  auto v = t[section][key].value<T>();
  if (!v) missingConfig(section, key);
  return *v;
}

int parseHHMM(const std::string& s) {
  int h = -1, m = -1;
  if (std::sscanf(s.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 ||
      m < 0 || m > 59) {
    throw std::runtime_error("Invalid HH:MM time: " + s);
  }
  return h * 100 + m;
}

}  // namespace

// ---------- AsyncPoster: background HTTP POST worker ----------

class AsyncPoster {
 public:
  AsyncPoster() : worker_([this] { run(); }) {}

  AsyncPoster(const AsyncPoster&) = delete;
  AsyncPoster& operator=(const AsyncPoster&) = delete;

  ~AsyncPoster() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  void post(std::string url, std::string body) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      queue_.emplace(std::move(url), std::move(body));
    }
    cv_.notify_one();
  }

 private:
  void run() {
    while (true) {
      std::pair<std::string, std::string> item;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (queue_.empty()) return;  // shutdown with empty queue
        item = std::move(queue_.front());
        queue_.pop();
      }
      doPost(item.first, item.second);
    }
  }

  static void doPost(const std::string& url, const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) {
      std::cerr << ts << "POST init failed for " << url << std::endl;
      return;
    }

    curl_slist* headers =
        curl_slist_append(nullptr, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kHttpTimeoutSec);
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
      std::cerr << ts << "POST to " << url
                << " failed: " << curl_easy_strerror(res) << " (HTTP "
                << status_code << ")" << std::endl;
    }
  }

  std::queue<std::pair<std::string, std::string>> queue_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool stop_ = false;
  std::thread worker_;
};

// ---------- LatencyMeasurement ----------

void LatencyMeasurement::loadConfig(const std::string& filename) {
  toml::table t;
  try {
    t = toml::parse_file(filename);
  } catch (const toml::parse_error& err) {
    throw std::runtime_error(
        std::format("Failed to parse {}: {}", filename, err.what()));
  }

  // [user]
  config_.user_id = required<std::string>(t, "user", "user_id");
  config_.password = required<std::string>(t, "user", "password");
  config_.account = required<std::string>(t, "user", "account");
  config_.pfx_filepath = required<std::string>(t, "user", "pfx_filepath");
  config_.pfx_password = required<std::string>(t, "user", "pfx_password");

  // [order]
  config_.symbol = required<std::string>(t, "order", "symbol");
  config_.price = required<double>(t, "order", "price");
  config_.quantity = required<int>(t, "order", "quantity");
  config_.side_str = required<std::string>(t, "order", "side");
  config_.price_str = std::to_string(config_.price);
  config_.quantity_str = std::to_string(config_.quantity);
  config_.market = concords_sdk::stock::parseMarket(
      required<std::string>(t, "order", "market"));
  config_.order_board = concords_sdk::stock::parseOrderBoard(
      required<std::string>(t, "order", "order_board"));
  config_.funding_type = concords_sdk::stock::parseFundingType(
      required<std::string>(t, "order", "funding_type"));
  config_.side = concords_sdk::stock::parseSide(config_.side_str);
  config_.order_type = concords_sdk::stock::parseOrderType(
      required<std::string>(t, "order", "order_type"));
  config_.time_in_force = concords_sdk::stock::parseTimeInForce(
      required<std::string>(t, "order", "time_in_force"));
  config_.daytrade_shortsell = concords_sdk::stock::parseDaytradeShortSell(
      required<std::string>(t, "order", "daytrade_shortsell"));

  // [api]
  config_.api_url = required<std::string>(t, "api", "url");
  config_.broker_name = required<std::string>(t, "api", "broker_name");

  // [trading_hours]
  config_.interval_seconds =
      required<int>(t, "trading_hours", "interval_seconds");
  config_.trading_start_str =
      required<std::string>(t, "trading_hours", "start_time");
  config_.trading_end_str =
      required<std::string>(t, "trading_hours", "end_time");
  config_.trading_start_hhmm = parseHHMM(config_.trading_start_str);
  config_.trading_end_hhmm = parseHHMM(config_.trading_end_str);

  // [network_probe]
  config_.probe_host = required<std::string>(t, "network_probe", "host");
  config_.probe_port = required<int>(t, "network_probe", "port");
}

LatencyMeasurement::LatencyMeasurement() {
  loadConfig();

  client_ = concords_sdk::stock::BuildStockClient(
      config_.user_id.c_str(), config_.password.c_str(),
      config_.account.c_str(), config_.pfx_filepath.c_str(),
      config_.pfx_password.c_str());

  network_probe_ =
      std::make_unique<NetworkProbe>(config_.probe_host, config_.probe_port);
  poster_ = std::make_unique<AsyncPoster>();

  client_->SetOrderSubmitCallback(
      [this](const concords_sdk::stock::OrderSubmitResult& result) {
        ack_time_ = chrono::high_resolution_clock::now();

        if (result.user_defined_id != current_order_id_) {
          std::cout << ts << "Bypassing order: " << result.user_defined_id
                    << std::endl;
          return;
        }

        sdk_local_ms_ = chrono::duration_cast<chrono::microseconds>(
                            submit_call_end_time_ - order_start_time_)
                            .count() /
                        1000.0;

        if (result.success) {
          ack_rtt_ms_ = chrono::duration_cast<chrono::microseconds>(
                            ack_time_ - submit_call_end_time_)
                            .count() /
                        1000.0;
          total_ms_ = chrono::duration_cast<chrono::microseconds>(
                          ack_time_ - order_start_time_)
                          .count() /
                      1000.0;
          outcome_ = "success";

          // Snapshot kernel TCP info from the SDK's order socket
          tcp_info_ = collectTcpInfo(config_.probe_port);

          std::cout << ts << "Order submitted! total=" << *total_ms_
                    << "ms sdk_local=" << *sdk_local_ms_
                    << "ms ack_rtt=" << *ack_rtt_ms_ << "ms" << std::endl;

          std::this_thread::sleep_for(kCancelDelay);
          cancel_call_time_ = chrono::high_resolution_clock::now();
          client_->CancelOrder(current_order_id_);
        } else {
          outcome_ = "submit_error";
          error_message_ = result.error_message;
          std::cerr << ts << "Order submission failed: " << result.error_message
                    << std::endl;
        }

        {
          std::lock_guard<std::mutex> lock(latency_mutex_);
          order_submitted_ = true;
        }
        cv_.notify_one();
      });

  client_->SetOrderCancelCallback(
      [this](const concords_sdk::stock::OrderCancelResult& result) {
        cancel_done_time_ = chrono::high_resolution_clock::now();
        cancel_rtt_ms_ = chrono::duration_cast<chrono::microseconds>(
                             cancel_done_time_ - cancel_call_time_)
                             .count() /
                         1000.0;

        if (result.success) {
          std::cout << ts << "Order cancelled, cancel_rtt=" << *cancel_rtt_ms_
                    << "ms" << std::endl;
        } else {
          outcome_ = "cancel_error";
          error_message_ = result.error_message;
          std::cerr << ts
                    << "Order cancellation failed: " << result.error_message
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
    std::cerr << ts << "Failed to connect to broker" << std::endl;
    return false;
  }
  std::cout << ts << "Successfully connected and authenticated with broker"
            << std::endl;
  return true;
}

void LatencyMeasurement::resetIterationState() {
  total_ms_.reset();
  sdk_local_ms_.reset();
  ack_rtt_ms_.reset();
  cancel_rtt_ms_.reset();
  outcome_.clear();
  error_message_.clear();
  tcp_info_ = {};
  order_submitted_ = false;
  order_cancelled_ = false;
}

void LatencyMeasurement::submitOrder() {
  auto now = chrono::floor<chrono::seconds>(chrono::system_clock::now());
  chrono::zoned_time local{chrono::current_zone(), now};
  current_order_id_ =
      std::format("test-{:%Y%m%d%H%M%S}-{}", local, order_counter_);

  concords_sdk::stock::OrderInfo order(
      config_.market, config_.order_board, config_.funding_type, config_.symbol,
      config_.side, config_.order_type, config_.time_in_force,
      config_.quantity_str, config_.price_str, config_.daytrade_shortsell,
      current_order_id_);

  // Submit
  order_start_time_ = chrono::high_resolution_clock::now();
  client_->SubmitOrder(order);
  submit_call_end_time_ = chrono::high_resolution_clock::now();

  // Wait for submit callback
  {
    std::unique_lock<std::mutex> lock(latency_mutex_);
    bool ok = cv_.wait_for(lock, kCallbackTimeout,
                           [this] { return order_submitted_.load(); });
    if (!ok) {
      outcome_ = "ack_timeout";
      std::cerr << ts << "Order submission timeout" << std::endl;
      sendOrderReport();
      return;
    }
  }

  if (outcome_ == "submit_error") {
    sendOrderReport();
    return;
  }

  // Wait for cancel callback
  {
    std::unique_lock<std::mutex> lock(latency_mutex_);
    bool ok = cancel_cv_.wait_for(lock, kCallbackTimeout,
                                  [this] { return order_cancelled_.load(); });
    if (!ok) {
      outcome_ = "cancel_timeout";
      std::cerr << ts << "Order cancellation timeout" << std::endl;
      sendOrderReport();
      return;
    }
  }

  sendOrderReport();
}

void LatencyMeasurement::runNetworkProbe() {
  NetworkProbeResult result = network_probe_->probe();
  if (result.cold_ok) {
    std::cout << ts << "Network probe: dns=" << *result.dns_ms
              << "ms tcp=" << *result.tcp_handshake_ms
              << "ms tls=" << *result.tls_handshake_ms << "ms";
    if (result.tls_handshake_resumed_ms.has_value()) {
      std::cout << " warm_tls=" << *result.tls_handshake_resumed_ms
                << "ms resumed="
                << (result.resumption_supported.value_or(false) ? "y" : "n");
    }
    std::cout << std::endl;
    if (!result.warm_error.empty()) {
      std::cerr << ts << "Warm probe failed: " << result.warm_error
                << std::endl;
    }
  } else {
    std::cerr << ts << "Network probe failed: " << result.error << std::endl;
  }
  sendNetworkReport(result);
}

void LatencyMeasurement::sendOrderReport() {
  std::string body = std::format(
      R"({{"metric_type":"order","timestamp":"{}","iteration_id":{},"broker":"{}",)"
      R"("outcome":"{}","error_message":{},)"
      R"("total_ms":{},"sdk_local_ms":{},"ack_rtt_ms":{},"cancel_rtt_ms":{},)"
      R"("tcp_rtt_us":{},"tcp_rttvar_us":{},"tcp_snd_cwnd":{},"tcp_retrans":{}}})",
      nowTimestamp(), order_counter_, config_.broker_name, outcome_,
      fmtOptString(error_message_), fmtOpt(total_ms_), fmtOpt(sdk_local_ms_),
      fmtOpt(ack_rtt_ms_), fmtOpt(cancel_rtt_ms_), fmtOpt(tcp_info_.rtt_us),
      fmtOpt(tcp_info_.rttvar_us), fmtOpt(tcp_info_.snd_cwnd),
      fmtOpt(tcp_info_.total_retrans));

  poster_->post(config_.api_url + "/order-metrics", std::move(body));
}

void LatencyMeasurement::sendNetworkReport(const NetworkProbeResult& result) {
  std::string body = std::format(
      R"({{"metric_type":"network","timestamp":"{}","iteration_id":{},"broker":"{}",)"
      R"("dns_ms":{},"tcp_handshake_ms":{},"tls_handshake_ms":{},)"
      R"("tls_handshake_resumed_ms":{},"resumption_supported":{},)"
      R"("error":{}}})",
      nowTimestamp(), order_counter_, config_.broker_name,
      fmtOpt(result.dns_ms), fmtOpt(result.tcp_handshake_ms),
      fmtOpt(result.tls_handshake_ms), fmtOpt(result.tls_handshake_resumed_ms),
      fmtOpt(result.resumption_supported), fmtOptString(result.error));

  poster_->post(config_.api_url + "/network-metrics", std::move(body));
}

void LatencyMeasurement::runLatencyTest() {
  std::cout << ts << "Trading: " << config_.symbol << " " << config_.side_str
            << " " << config_.price << " x" << config_.quantity << " every "
            << config_.interval_seconds << "s during "
            << config_.trading_start_str << "-" << config_.trading_end_str
            << std::endl;

  while (true) {
    auto now = chrono::system_clock::now();
    auto utc8_now = now + chrono::hours(8);
    auto utc8_dp = chrono::floor<chrono::days>(utc8_now);
    chrono::weekday wd{utc8_dp};
    chrono::hh_mm_ss hms{utc8_now - utc8_dp};

    int current_time = hms.hours().count() * 100 + hms.minutes().count();
    bool is_weekday = (wd != chrono::Saturday && wd != chrono::Sunday);
    bool is_trading_time = (current_time >= config_.trading_start_hhmm &&
                            current_time <= config_.trading_end_hhmm);

    if (!is_weekday || !is_trading_time) {
      std::this_thread::sleep_for(kMarketClosedRecheckInterval);
      continue;
    }

    order_counter_++;
    std::cout << "\n"
              << ts << "--- Iteration #" << order_counter_ << " ---"
              << std::endl;
    resetIterationState();
    runNetworkProbe();
    submitOrder();
    std::this_thread::sleep_for(chrono::seconds(config_.interval_seconds));
  }
}
