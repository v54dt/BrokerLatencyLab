#include <sys/syscall.h>
#include <toml++/toml.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "mono_clock.h"
#include "price_selection.h"
#include "round_engine.h"
#include "sdk_includes.h"
#include "ticker/include/ticker.h"

namespace {

using namespace std::chrono_literals;

std::atomic<bool> g_sigint{false};

void OnSigint(int) { g_sigint.store(true); }

long Gettid() { return static_cast<long>(::syscall(SYS_gettid)); }

struct Config {
  std::string user_id, password, account, pfx_filepath, pfx_password;

  std::string symbol;
  bool is_buy = true;
  concords_sdk::stock::Market market{};
  concords_sdk::stock::OrderBoard order_board{};
  concords_sdk::stock::FundingType funding_type{};
  concords_sdk::stock::Side side{};
  concords_sdk::stock::OrderType order_type{};
  concords_sdk::stock::TimeInForce time_in_force{};
  concords_sdk::stock::DaytradeShortSell daytrade_shortsell{};

  int rounds = 10;
  int interval_seconds = 5;
  int stage_timeout_seconds = 10;
  int run_deadline_seconds = 240;
  int session_start_minutes = 0;
  int session_end_minutes = 0;
  int quantity = 1;
  tick_to_trade::SelectionConfig selection;
  std::string csv_dir = "output";
};

int ParseHhmm(const std::string& s) {
  int h = -1, m = -1;
  if (std::sscanf(s.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 ||
      m < 0 || m > 59) {
    std::fprintf(stderr, "invalid HH:MM: %s\n", s.c_str());
    std::exit(1);
  }
  return h * 60 + m;
}

Config LoadConfig(const std::string& path) {
  toml::table t;
  try {
    t = toml::parse_file(path);
  } catch (const toml::parse_error& err) {
    std::fprintf(stderr, "failed to parse %s: %s\n", path.c_str(), err.what());
    std::exit(1);
  }
  const auto req = [&](const char* section, const char* key) -> std::string {
    const auto v = t[section][key].value<std::string>();
    if (!v) {
      std::fprintf(stderr, "missing config key: %s.%s\n", section, key);
      std::exit(1);
    }
    return *v;
  };
  const auto req_int = [&](const char* key, int fallback) {
    return t["tick_to_trade"][key].value<int>().value_or(fallback);
  };

  Config c;
  c.user_id = req("user", "user_id");
  c.password = req("user", "password");
  c.account = req("user", "account");
  c.pfx_filepath = req("user", "pfx_filepath");
  c.pfx_password = req("user", "pfx_password");

  c.symbol = req("order", "symbol");
  c.market = concords_sdk::stock::parseMarket(req("order", "market"));
  c.order_board =
      concords_sdk::stock::parseOrderBoard(req("order", "order_board"));
  c.funding_type =
      concords_sdk::stock::parseFundingType(req("order", "funding_type"));
  c.side = concords_sdk::stock::parseSide(req("order", "side"));
  c.is_buy = c.side == concords_sdk::stock::Side::Buy;
  c.order_type =
      concords_sdk::stock::parseOrderType(req("order", "order_type"));
  c.time_in_force =
      concords_sdk::stock::parseTimeInForce(req("order", "time_in_force"));
  c.daytrade_shortsell = concords_sdk::stock::parseDaytradeShortSell(
      req("order", "daytrade_shortsell"));

  c.rounds = req_int("rounds", 10);
  c.interval_seconds = req_int("interval_seconds", 5);
  c.stage_timeout_seconds = req_int("stage_timeout_seconds", 10);
  c.run_deadline_seconds = req_int("run_deadline_seconds", 240);
  c.session_start_minutes = ParseHhmm(
      t["tick_to_trade"]["session_start"].value<std::string>().value_or(
          "09:05"));
  c.session_end_minutes = ParseHhmm(
      t["tick_to_trade"]["session_end"].value<std::string>().value_or("13:20"));
  c.quantity = req_int("quantity", 1);
  c.selection.min_ticks_from_touch = req_int("min_ticks_from_touch", 2);
  c.selection.max_ticks_from_touch = req_int("max_ticks_from_touch", 10);
  c.csv_dir =
      t["tick_to_trade"]["csv_dir"].value<std::string>().value_or("output");
  return c;
}

// Session gate runs on fixed UTC+8 regardless of host TZ.
int TaipeiMinutesNow() {
  const std::time_t now = std::time(nullptr) + 8 * 3600;
  std::tm tm_utc8{};
  gmtime_r(&now, &tm_utc8);
  return tm_utc8.tm_hour * 60 + tm_utc8.tm_min;
}

bool InSession(const Config& c) {
  const int m = TaipeiMinutesNow();
  return m >= c.session_start_minutes && m <= c.session_end_minutes;
}

class CsvWriter {
 public:
  bool Open(const std::string& dir, std::uint64_t run_id, const Config& c,
            tick_to_trade::ClockAnchor anchor) {
    std::filesystem::create_directories(dir);
    const std::string path = dir + "/t2t_" + std::to_string(run_id) + ".csv";
    file_ = std::fopen(path.c_str(), "w");
    if (!file_) {
      std::fprintf(stderr, "cannot open %s\n", path.c_str());
      return false;
    }
    std::fprintf(file_,
                 "# run_id=%llu symbol=%s side=%s quantity=%d "
                 "interval_seconds=%d mono_anchor_ns=%llu "
                 "realtime_anchor_ns=%llu\n",
                 static_cast<unsigned long long>(run_id), c.symbol.c_str(),
                 c.is_buy ? "Buy" : "Sell", c.quantity, c.interval_seconds,
                 static_cast<unsigned long long>(anchor.mono_ns),
                 static_cast<unsigned long long>(anchor.realtime_ns));
    std::fprintf(
        file_,
        "round_id,client_order_id_1,price_1,qty_1,T1,T2,"
        "client_order_id_2,price_2,qty_2,T3,T4,"
        "outcome,submit1_return_ns,submit2_return_ns,"
        "ack1_local_ns,ack2_local_ns,ack1_ok,ack2_ok,"
        "ack1_broker_ts,ack2_broker_ts,Tp1_ns,Tp2_ns,"
        "qty_match_1,qty_match_2,saw_trial,book_quiesced,precheck_ok,"
        "cancel1_ok,cancel2_ok,o2_submit_in_flight,ticker_tid,stock_cb_tid\n");
    std::fflush(file_);
    std::printf("csv: %s\n", path.c_str());
    return true;
  }

  void WriteRow(const tick_to_trade::RoundResult& r, bool book_quiesced,
                bool precheck_ok, long ticker_tid, long stock_cb_tid) {
    std::fprintf(file_,
                 "%llu,%s,%s,%d,%llu,%llu,%s,%s,%d,%llu,%llu,"
                 "%s,%llu,%llu,%llu,%llu,%d,%d,%s,%s,%lld,%lld,"
                 "%d,%d,%d,%d,%d,%d,%d,%d,%ld,%ld\n",
                 static_cast<unsigned long long>(r.round_id), r.o1_id.c_str(),
                 r.price1_text.c_str(), r.quantity,
                 static_cast<unsigned long long>(r.t1),
                 static_cast<unsigned long long>(r.t2), r.o2_id.c_str(),
                 r.price2_text.c_str(), r.quantity,
                 static_cast<unsigned long long>(r.t3),
                 static_cast<unsigned long long>(r.t4),
                 tick_to_trade::OutcomeName(r.outcome),
                 static_cast<unsigned long long>(r.submit1_return_ns),
                 static_cast<unsigned long long>(r.submit2_return_ns),
                 static_cast<unsigned long long>(r.ack1_local_ns),
                 static_cast<unsigned long long>(r.ack2_local_ns), r.ack1_ok,
                 r.ack2_ok, r.ack1_broker_ts.c_str(), r.ack2_broker_ts.c_str(),
                 static_cast<long long>(r.tp1_ns),
                 static_cast<long long>(r.tp2_ns), r.qty_match_1, r.qty_match_2,
                 r.saw_trial, book_quiesced, precheck_ok, r.cancel1_ok,
                 r.cancel2_ok, r.o2_submit_in_flight, ticker_tid, stock_cb_tid);
    std::fflush(file_);
  }

  ~CsvWriter() {
    if (file_) std::fclose(file_);
  }

 private:
  std::FILE* file_ = nullptr;
};

bool IsOpenStatus(concords_sdk::stock::OrderStatus status) {
  using concords_sdk::stock::OrderStatus;
  return status == OrderStatus::Submitted ||
         status == OrderStatus::PartialFilled ||
         status == OrderStatus::SubmitPending ||
         status == OrderStatus::PreOrderSubmitted;
}

// Run-end safety sweep; also the o2_submit_in_flight recovery path.
int CancelAllOpenOrders(concords_sdk::stock::StockClient& client) {
  const auto orders = client.GetOrders();
  if (!orders || !orders->status) {
    std::fprintf(stderr, "cancel-all sweep: GetOrders failed: %s\n",
                 orders ? orders->error_message.c_str() : "no response");
    return -1;
  }
  int open = 0;
  for (const concords_sdk::stock::OrderState& o : orders->responses) {
    if (!IsOpenStatus(o.order_status)) continue;
    ++open;
    std::printf("cancel-all: cancelling %s (%s %s x%s)\n", o.order_id.c_str(),
                o.symbol.c_str(), o.price.c_str(), o.quantity.c_str());
    client.CancelOrder(o.order_id);
    std::this_thread::sleep_for(500ms);
  }
  return open;
}

int Usage(int code) {
  std::fprintf(stderr,
               "usage: tick-to-trade [--connect-only] [--config <path>]\n");
  return code;
}

}  // namespace

int main(int argc, char** argv) {
  bool connect_only = false;
  std::string config_path = "config.toml";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--connect-only") {
      connect_only = true;
    } else if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      return Usage(0);
    } else {
      return Usage(2);
    }
  }

  const Config config = LoadConfig(config_path);
  const std::uint64_t run_id = static_cast<std::uint64_t>(std::time(nullptr));

  const std::unique_ptr<concords_sdk::stock::StockClient> client =
      concords_sdk::stock::BuildStockClient(
          config.user_id.c_str(), config.password.c_str(),
          config.account.c_str(), config.pfx_filepath.c_str(),
          config.pfx_password.c_str());

  tick_to_trade::RoundEngine engine(
      tick_to_trade::RoundEngine::RunConfig{config.symbol, config.is_buy},
      tick_to_trade::RoundEngine::Deps{
          [&](const tick_to_trade::OrderSpec& spec) {
            concords_sdk::stock::OrderInfo order(
                config.market, config.order_board, config.funding_type,
                config.symbol, config.side, config.order_type,
                config.time_in_force, spec.quantity, spec.price_text,
                config.daytrade_shortsell, spec.user_defined_id);
            client->SubmitOrder(order);
          },
          [&](const std::string& target_id) { client->CancelOrder(target_id); },
      });

  std::atomic<long> ticker_tid{0}, stock_cb_tid{0};
  std::atomic<std::uint64_t> quotation_count{0};

  client->SetOrderSubmitCallback(
      [&](const concords_sdk::stock::OrderSubmitResult& result) {
        stock_cb_tid.store(Gettid(), std::memory_order_relaxed);
        engine.OnSubmitAck(result.user_defined_id, result.success,
                           result.timestamp);
      });
  client->SetOrderCancelCallback(
      [&](const concords_sdk::stock::OrderCancelResult& result) {
        engine.OnCancelAck(result.target_id, result.success);
      });
  client->SetOrderFillCallback(
      [&](const concords_sdk::stock::OrderFillResult& result) {
        std::fprintf(stderr, "FILL %s %s x%s (%s) -> aborting run\n",
                     result.user_defined_id.c_str(), result.price.c_str(),
                     result.quantity.c_str(), result.timestamp.c_str());
        engine.OnFill();
      });

  if (!client->Connect()) {
    std::fprintf(stderr, "stock client connect failed\n");
    return 1;
  }
  std::printf("stock client connected\n");

  const std::unique_ptr<concords_sdk::ticker::Ticker> ticker =
      concords_sdk::ticker::BuildTicker(config.user_id.c_str(),
                                        config.password.c_str(),
                                        config.pfx_filepath.c_str());
  ticker->SetErrorCallback([&](const std::string& error) {
    std::fprintf(stderr, "ticker error: %s -> aborting run\n", error.c_str());
    engine.RequestAbort(tick_to_trade::Outcome::kAbortedError);
  });
  ticker->SetQuotationCallback(
      [&](const concords_sdk::ticker::Quotation& quotation) {
        ticker_tid.store(Gettid(), std::memory_order_relaxed);
        quotation_count.fetch_add(1, std::memory_order_relaxed);
        engine.OnQuotation(quotation);
      });
  if (!ticker->Subscribe(config.symbol.c_str())) {
    std::fprintf(stderr, "subscribe %s failed\n", config.symbol.c_str());
    return 1;
  }
  std::printf("subscribed %s, main_tid=%ld\n", config.symbol.c_str(), Gettid());

  if (connect_only) {
    for (int i = 0; i < 10 && !g_sigint.load(); ++i) {
      std::this_thread::sleep_for(1s);
    }
    const auto orders = client->GetOrders();
    std::printf(
        "connect-only: quotations=%llu ticker_tid=%ld stock_cb_tid=%ld "
        "orders=%s snapshot=%s taipei_minutes=%d in_session=%d\n",
        static_cast<unsigned long long>(quotation_count.load()),
        ticker_tid.load(), stock_cb_tid.load(),
        orders && orders->status
            ? std::to_string(orders->responses.size()).c_str()
            : "query_failed",
        engine.LatestSnapshot() ? "yes" : "none", TaipeiMinutesNow(),
        InSession(config));
    client->Disconnect();
    return 0;
  }

  std::signal(SIGINT, OnSigint);
  std::thread sigint_watcher([&] {
    while (!g_sigint.load() && !engine.aborted()) {
      std::this_thread::sleep_for(50ms);
    }
    if (g_sigint.load()) {
      engine.RequestAbort(tick_to_trade::Outcome::kAbortedSignal);
    }
  });

  CsvWriter csv;
  const tick_to_trade::ClockAnchor anchor = tick_to_trade::TakeClockAnchor();
  if (!csv.Open(config.csv_dir, run_id, config, anchor)) return 1;

  const tick_to_trade::StageTimeouts timeouts{
      std::chrono::seconds(config.stage_timeout_seconds),
      std::chrono::seconds(config.stage_timeout_seconds),
      std::chrono::seconds(config.stage_timeout_seconds)};
  const std::uint64_t deadline_ns =
      tick_to_trade::MonoNs() +
      static_cast<std::uint64_t>(config.run_deadline_seconds) *
          1'000'000'000ull;

  tick_to_trade::PriceTracker tracker;
  std::vector<tick_to_trade::Price> prev_prices;
  std::uint64_t prev_round_end_ns = 0;
  int consecutive_skips = 0;
  int completed = 0, ok_rounds = 0;

  for (int round = 1; round <= config.rounds; ++round) {
    if (engine.aborted() || g_sigint.load()) break;
    if (tick_to_trade::MonoNs() > deadline_ns) {
      std::printf("run deadline reached\n");
      break;
    }
    if (!InSession(config)) {
      std::printf("outside session window (taipei_minutes=%d), stopping\n",
                  TaipeiMinutesNow());
      break;
    }

    const auto stage_timeout =
        std::chrono::seconds(config.stage_timeout_seconds);

    bool book_quiesced = true;
    if (!prev_prices.empty()) {
      book_quiesced =
          engine.WaitBookClear(prev_prices, stage_timeout, prev_round_end_ns);
    }

    tick_to_trade::RoundResult result;
    result.round_id = static_cast<std::uint64_t>(round);
    bool precheck_ok = false;

    if (!engine.WaitForSnapshot(stage_timeout)) {
      result.outcome = tick_to_trade::Outcome::kNoBookSnapshot;
    } else {
      const auto snapshot = engine.LatestSnapshot();
      const auto touch =
          tick_to_trade::BestTouch(snapshot->view, config.is_buy);
      if (!touch) {
        result.outcome = tick_to_trade::Outcome::kNoReferencePrice;
      } else {
        const auto selected = tick_to_trade::SelectPricesFromBook(
            snapshot->view, config.is_buy, config.selection,
            [&](tick_to_trade::Price p) { return tracker.IsBlocked(p); });
        if (!selected) {
          result.outcome = tick_to_trade::Outcome::kNoEligiblePrice;
        } else {
          precheck_ok = true;
          consecutive_skips = 0;
          tracker.MarkInUse(selected->p1);
          tracker.MarkInUse(selected->p2);

          tick_to_trade::RoundPlan plan;
          plan.round_id = static_cast<std::uint64_t>(round);
          const std::string base =
              "t2t-" + std::to_string(run_id) + "-" + std::to_string(round);
          plan.o1 = tick_to_trade::OrderSpec{
              base + "-1", tick_to_trade::FormatPrice(selected->p1),
              selected->p1, config.quantity};
          plan.o2 = tick_to_trade::OrderSpec{
              base + "-2", tick_to_trade::FormatPrice(selected->p2),
              selected->p2, config.quantity};

          result = engine.RunRound(plan, timeouts);
          prev_round_end_ns = tick_to_trade::MonoNs();
          prev_prices = {selected->p1, selected->p2};

          if (result.cancel1_ok) {
            tracker.Release(selected->p1);
          } else {
            tracker.Taint(selected->p1);
          }
          if (result.cancel2_ok) {
            tracker.Release(selected->p2);
          } else {
            tracker.Taint(selected->p2);
          }
        }
      }
    }

    if (!precheck_ok) ++consecutive_skips;
    ++completed;
    if (result.outcome == tick_to_trade::Outcome::kOk) ++ok_rounds;
    csv.WriteRow(result, book_quiesced, precheck_ok, ticker_tid.load(),
                 stock_cb_tid.load());
    std::printf("round %d: %s\n", round,
                tick_to_trade::OutcomeName(result.outcome));

    if (result.o2_submit_in_flight) {
      std::fprintf(stderr,
                   "!!! o2 submit stuck in SDK (%s). keeping process alive; "
                   "sweeping GetOrders until it can be cancelled.\n",
                   result.o2_id.c_str());
      for (int attempt = 0; attempt < 6 && !g_sigint.load(); ++attempt) {
        std::this_thread::sleep_for(10s);
        if (CancelAllOpenOrders(*client) == 0) break;
      }
      std::fprintf(stderr, "!!! verify order %s manually before market close\n",
                   result.o2_id.c_str());
      break;
    }
    if (consecutive_skips >= 3) {
      std::fprintf(stderr, "3 consecutive skips, stopping run\n");
      break;
    }

    for (int s = 0; s < config.interval_seconds * 10 && !g_sigint.load() &&
                    !engine.aborted();
         ++s) {
      std::this_thread::sleep_for(100ms);
    }
  }

  std::printf("run finished: %d rounds written, %d ok\n", completed, ok_rounds);
  if (engine.unmatched_cancel_acks() > 0) {
    std::fprintf(
        stderr, "unmatched cancel acks: %llu (check target_id semantics)\n",
        static_cast<unsigned long long>(engine.unmatched_cancel_acks()));
  }
  const int open = CancelAllOpenOrders(*client);
  if (open > 0) {
    std::this_thread::sleep_for(2s);
    if (CancelAllOpenOrders(*client) != 0) {
      std::fprintf(stderr, "!!! open orders may remain, check manually\n");
    }
  }

  g_sigint.store(true);
  sigint_watcher.join();
  client->Disconnect();
  return 0;
}
