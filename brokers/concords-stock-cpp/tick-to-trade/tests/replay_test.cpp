#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../mono_clock.h"
#include "../price_selection.h"
#include "../round_engine.h"

namespace tick_to_trade {
namespace {
using namespace std::chrono_literals;

Price P(const char* s) {
  const std::optional<Price> p = ParsePrice(s);
  EXPECT_TRUE(p.has_value()) << "ParsePrice(\"" << s << "\")";
  return p.value_or(Price{});
}

concords_sdk::ticker::PriceVolume PV(const char* price, unsigned int volume) {
  const Price p = P(price);
  return {{static_cast<int>(p.digits), p.precision}, volume};
}

class FakeQuotation : public concords_sdk::ticker::Quotation {
 public:
  std::string product = "2897";
  bool trial = false;
  std::int64_t tp_us = 1'700'000'000'000'000;
  std::vector<concords_sdk::ticker::PriceVolume> trades, bids, asks;

  std::chrono::time_point<std::chrono::system_clock> GetTimestamp()
      const override {
    return std::chrono::time_point<std::chrono::system_clock>(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::microseconds(tp_us)));
  }
  const char* GetProductId() const override { return product.c_str(); }
  concords_sdk::ticker::Exchange GetExchange() const override {
    return concords_sdk::ticker::Exchange::kTWSE;
  }
  std::size_t GetTradeSize() const override { return trades.size(); }
  concords_sdk::ticker::PriceVolume GetTrade(std::size_t i) const override {
    return trades[i];
  }
  bool IsTrial() const override { return trial; }
  std::size_t GetBidSize() const override { return bids.size(); }
  concords_sdk::ticker::PriceVolume GetBid(std::size_t i) const override {
    return bids[i];
  }
  std::size_t GetAskSize() const override { return asks.size(); }
  concords_sdk::ticker::PriceVolume GetAsk(std::size_t i) const override {
    return asks[i];
  }
};

FakeQuotation BidQuote(std::vector<concords_sdk::ticker::PriceVolume> bids) {
  FakeQuotation q;
  q.bids = std::move(bids);
  return q;
}

FakeQuotation AskQuote(std::vector<concords_sdk::ticker::PriceVolume> asks) {
  FakeQuotation q;
  q.asks = std::move(asks);
  return q;
}

struct Harness {
  std::mutex m;
  std::vector<OrderSpec> submits;
  std::vector<std::string> cancels;
  bool auto_ack = true;
  bool auto_cancel_ack = true;
  std::function<bool(const std::string&)> ack_ok = [](const std::string&) {
    return true;
  };
  std::function<bool(const std::string&)> should_ack = [](const std::string&) {
    return true;
  };
  std::function<void(const OrderSpec&)> submit_hook;
  RoundEngine engine;

  explicit Harness(bool is_buy = true)
      : engine(
            RoundEngine::RunConfig{"2897", is_buy},
            RoundEngine::Deps{[this](const OrderSpec& s) {
                                {
                                  std::lock_guard<std::mutex> lock(m);
                                  submits.push_back(s);
                                }
                                if (submit_hook) submit_hook(s);
                                if (auto_ack && should_ack(s.user_defined_id)) {
                                  engine.OnSubmitAck(s.user_defined_id,
                                                     ack_ok(s.user_defined_id),
                                                     "09:05:00");
                                }
                              },
                              [this](const std::string& target) {
                                {
                                  std::lock_guard<std::mutex> lock(m);
                                  cancels.push_back(target);
                                }
                                if (auto_cancel_ack)
                                  engine.OnCancelAck(target, true);
                              }}) {}

  std::size_t SubmitCount() {
    std::lock_guard<std::mutex> lock(m);
    return submits.size();
  }
  std::vector<std::string> SubmitIds() {
    std::lock_guard<std::mutex> lock(m);
    std::vector<std::string> ids;
    for (const auto& s : submits) ids.push_back(s.user_defined_id);
    return ids;
  }
  std::vector<std::string> Cancels() {
    std::lock_guard<std::mutex> lock(m);
    return cancels;
  }
};

bool WaitUntil(const std::function<bool()>& pred,
               std::chrono::milliseconds timeout = 2000ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(1ms);
  }
  return pred();
}

RoundPlan MakePlan(std::uint64_t round_id, const char* p1, const char* p2,
                   int qty = 1) {
  RoundPlan plan;
  plan.round_id = round_id;
  plan.o1 =
      OrderSpec{"t2t-test-" + std::to_string(round_id) + "-1", p1, P(p1), qty};
  plan.o2 =
      OrderSpec{"t2t-test-" + std::to_string(round_id) + "-2", p2, P(p2), qty};
  return plan;
}

constexpr StageTimeouts kFastTimeouts{300ms, 400ms, 300ms};

TEST(RoundEngineTest, HappyPath) {
  Harness h;
  const RoundPlan plan = MakePlan(1, "10.05", "10.15");
  RoundResult r;
  std::thread runner([&] { r = h.engine.RunRound(plan, kFastTimeouts); });

  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));

  FakeQuotation other = BidQuote({PV("10.05", 1)});
  other.product = "9999";
  h.engine.OnQuotation(other);
  FakeQuotation trial = BidQuote({PV("10.40", 3), PV("10.05", 1)});
  trial.trial = true;
  h.engine.OnQuotation(trial);
  h.engine.OnQuotation(BidQuote({PV("10.40", 3)}));
  std::this_thread::sleep_for(20ms);
  EXPECT_TRUE(h.SubmitCount() == 1);

  FakeQuotation reveal1 = BidQuote({PV("10.40", 3), PV("10.05", 1)});
  reveal1.tp_us = 1'700'000'000'000'111;
  h.engine.OnQuotation(reveal1);
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2; }));

  FakeQuotation reveal2 =
      BidQuote({PV("10.40", 3), PV("10.15", 1), PV("10.05", 1)});
  reveal2.tp_us = 1'700'000'000'000'222;
  h.engine.OnQuotation(reveal2);

  runner.join();

  EXPECT_TRUE(r.outcome == Outcome::kOk);
  EXPECT_TRUE(r.t1 != 0 && r.t2 != 0 && r.t3 != 0 && r.t4 != 0);
  EXPECT_TRUE(r.t1 < r.t2 && r.t2 <= r.t3 && r.t3 < r.submit2_return_ns);
  EXPECT_TRUE(r.t3 < r.t4);
  EXPECT_TRUE(r.submit1_return_ns > r.t1);
  EXPECT_TRUE(
      (h.SubmitIds() == std::vector<std::string>{plan.o1.user_defined_id,
                                                 plan.o2.user_defined_id}));
  EXPECT_TRUE(
      (h.Cancels() == std::vector<std::string>{plan.o1.user_defined_id,
                                               plan.o2.user_defined_id}));
  EXPECT_TRUE(r.ack1_ok && r.ack2_ok);
  EXPECT_TRUE(r.ack1_local_ns != 0 && r.ack2_local_ns != 0);
  EXPECT_TRUE(r.ack1_broker_ts == "09:05:00");
  EXPECT_TRUE(r.cancel1_ok && r.cancel2_ok);
  EXPECT_TRUE(r.qty_match_1 && r.qty_match_2);
  EXPECT_TRUE(r.saw_trial);
  EXPECT_TRUE(r.tp1_ns == 1'700'000'000'000'111 * 1000);
  EXPECT_TRUE(r.tp2_ns == 1'700'000'000'000'222 * 1000);
  EXPECT_TRUE(std::string(OutcomeName(r.outcome)) == "ok");
}

TEST(RoundEngineTest, Reveal1TimeoutAndPostAbandonToctou) {
  Harness h;
  const RoundPlan plan = MakePlan(2, "10.05", "10.15");
  const RoundResult r = h.engine.RunRound(plan, {100ms, 50ms, 100ms});

  EXPECT_TRUE(r.outcome == Outcome::kReveal1Timeout);
  EXPECT_TRUE(r.t2 == 0 && r.t3 == 0 && r.t4 == 0);
  EXPECT_TRUE(
      (h.Cancels() == std::vector<std::string>{plan.o1.user_defined_id,
                                               plan.o2.user_defined_id}));

  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  std::this_thread::sleep_for(20ms);
  EXPECT_TRUE(h.SubmitCount() == 1);
}

TEST(RoundEngineTest, SubmitError1) {
  Harness h;
  h.ack_ok = [](const std::string& id) { return id.back() != '1'; };
  const RoundPlan plan = MakePlan(3, "10.05", "10.15");
  const RoundResult r = h.engine.RunRound(plan, kFastTimeouts);

  EXPECT_TRUE(r.outcome == Outcome::kSubmitError1);
  EXPECT_TRUE(!r.ack1_ok);
  EXPECT_TRUE(h.SubmitCount() == 1);
  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  std::this_thread::sleep_for(20ms);
  EXPECT_TRUE(h.SubmitCount() == 1);
  EXPECT_TRUE(h.Cancels().size() == 2);
}

TEST(RoundEngineTest, SubmitError2) {
  Harness h;
  h.ack_ok = [](const std::string& id) { return id.back() != '2'; };
  const RoundPlan plan = MakePlan(4, "10.05", "10.15");
  RoundResult r;
  std::thread runner([&] { r = h.engine.RunRound(plan, kFastTimeouts); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  runner.join();

  EXPECT_TRUE(r.outcome == Outcome::kSubmitError2);
  EXPECT_TRUE(r.ack1_ok && !r.ack2_ok);
  EXPECT_TRUE(h.SubmitCount() == 2);
  EXPECT_TRUE(r.t2 != 0 && r.t3 != 0 && r.t4 == 0);
}

TEST(RoundEngineTest, Reveal2Timeout) {
  Harness h;
  const RoundPlan plan = MakePlan(5, "10.05", "10.15");
  RoundResult r;
  std::thread runner(
      [&] { r = h.engine.RunRound(plan, {300ms, 80ms, 300ms}); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2; }));
  runner.join();

  EXPECT_TRUE(r.outcome == Outcome::kReveal2Timeout);
  EXPECT_TRUE(r.t2 != 0 && r.t3 != 0 && r.t4 == 0);
  EXPECT_TRUE(h.Cancels().size() == 2);
}

TEST(RoundEngineTest, FillAbort) {
  Harness h;
  const RoundPlan plan = MakePlan(6, "10.05", "10.15");
  RoundResult r;
  std::thread runner([&] { r = h.engine.RunRound(plan, kFastTimeouts); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  h.engine.OnFill();
  runner.join();

  EXPECT_TRUE(r.outcome == Outcome::kAbortedFill);
  EXPECT_TRUE(h.engine.aborted());
  EXPECT_TRUE(h.Cancels().size() == 2);
  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  std::this_thread::sleep_for(20ms);
  EXPECT_TRUE(h.SubmitCount() == 1);
}

TEST(RoundEngineTest, SignalAbort) {
  Harness h;
  const RoundPlan plan = MakePlan(7, "10.05", "10.15");
  RoundResult r;
  std::thread runner([&] { r = h.engine.RunRound(plan, kFastTimeouts); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  h.engine.RequestAbort(Outcome::kAbortedSignal);
  runner.join();
  EXPECT_TRUE(r.outcome == Outcome::kAbortedSignal);
  h.engine.RequestAbort(Outcome::kAbortedError);
  EXPECT_TRUE(h.engine.abort_reason() == Outcome::kAbortedSignal);
}

TEST(RoundEngineTest, SnapshotAndBookClear) {
  Harness h;
  EXPECT_TRUE(!h.engine.WaitForSnapshot(30ms));
  h.engine.OnQuotation(BidQuote({PV("10.40", 3), PV("10.05", 1)}));
  EXPECT_TRUE(h.engine.WaitForSnapshot(100ms));
  const auto snap = h.engine.LatestSnapshot();
  EXPECT_TRUE(snap.has_value() && snap->view.bids.size() == 2);
  EXPECT_TRUE(snap->mono_ns != 0);

  EXPECT_TRUE(!h.engine.WaitBookClear({P("10.05")}, 30ms));
  h.engine.OnQuotation(BidQuote({PV("10.40", 3)}));
  EXPECT_TRUE(h.engine.WaitBookClear({P("10.05")}, 100ms));

  const std::uint64_t threshold = MonoNs();
  EXPECT_TRUE(!h.engine.WaitForSnapshot(30ms, threshold));
  EXPECT_TRUE(!h.engine.WaitBookClear({P("10.05")}, 30ms, threshold));
  h.engine.OnQuotation(BidQuote({PV("10.40", 4)}));
  EXPECT_TRUE(h.engine.WaitForSnapshot(100ms, threshold));
  EXPECT_TRUE(h.engine.WaitBookClear({P("10.05")}, 100ms, threshold));
}

TEST(RoundEngineTest, TrialNeverBecomesSnapshot) {
  Harness h;
  FakeQuotation t = BidQuote({PV("10.40", 1)});
  t.trial = true;
  h.engine.OnQuotation(t);
  EXPECT_TRUE(!h.engine.WaitForSnapshot(30ms));
  h.engine.OnQuotation(BidQuote({PV("10.40", 1)}));
  EXPECT_TRUE(h.engine.WaitForSnapshot(100ms));
  EXPECT_TRUE(!h.engine.LatestSnapshot()->view.is_trial);
}

TEST(RoundEngineTest, BookClearSell) {
  Harness h(false);
  h.engine.OnQuotation(AskQuote({PV("10.60", 1)}));
  EXPECT_TRUE(h.engine.WaitForSnapshot(100ms));
  EXPECT_TRUE(!h.engine.WaitBookClear({P("10.60")}, 30ms));
  FakeQuotation q;
  q.bids = {PV("10.60", 1)};
  q.asks = {PV("10.70", 1)};
  h.engine.OnQuotation(q);
  EXPECT_TRUE(h.engine.WaitBookClear({P("10.60")}, 100ms));
}

TEST(RoundEngineTest, SnapshotInvalidatedByArm) {
  Harness h;
  h.engine.OnQuotation(BidQuote({PV("10.40", 1)}));
  EXPECT_TRUE(h.engine.WaitForSnapshot(100ms));

  const RoundPlan plan = MakePlan(60, "10.05", "10.15");
  RoundResult r;
  std::thread runner([&] { r = h.engine.RunRound(plan, kFastTimeouts); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2; }));
  h.engine.OnQuotation(BidQuote({PV("10.15", 1), PV("10.05", 1)}));
  runner.join();
  EXPECT_TRUE(r.outcome == Outcome::kOk);
  EXPECT_TRUE(!h.engine.WaitBookClear({P("10.05")}, 30ms));
  h.engine.OnQuotation(BidQuote({PV("10.40", 1)}));
  EXPECT_TRUE(h.engine.WaitBookClear({P("10.05"), P("10.15")}, 100ms));
}

TEST(RoundEngineTest, TwoRoundsBackToBack) {
  Harness h;
  for (std::uint64_t round = 1; round <= 2; ++round) {
    const RoundPlan plan = MakePlan(100 + round, "10.05", "10.15");
    RoundResult r;
    std::thread runner([&] { r = h.engine.RunRound(plan, kFastTimeouts); });
    EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2 * round - 1; }));
    if (round == 1) {
      FakeQuotation t = BidQuote({PV("10.05", 1)});
      t.trial = true;
      h.engine.OnQuotation(t);
    }
    h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
    EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2 * round; }));
    h.engine.OnQuotation(BidQuote({PV("10.15", 1), PV("10.05", 1)}));
    runner.join();
    EXPECT_TRUE(r.outcome == Outcome::kOk);
    EXPECT_TRUE(r.saw_trial == (round == 1));
    EXPECT_TRUE(r.t2 != 0 && r.t4 != 0);
  }
  const RoundResult r3 =
      h.engine.RunRound(MakePlan(103, "10.05", "10.15"), {200ms, 60ms, 200ms});
  EXPECT_TRUE(r3.outcome == Outcome::kReveal1Timeout);
  EXPECT_TRUE(r3.t2 == 0 && r3.t3 == 0 && r3.t4 == 0);
  EXPECT_TRUE(r3.tp1_ns == 0 && r3.tp2_ns == 0);
  EXPECT_TRUE(!r3.qty_match_1 && !r3.qty_match_2 && !r3.saw_trial);
  h.engine.OnSubmitAck("t2t-test-101-1", false, "");
  EXPECT_TRUE(!h.engine.aborted());
}

TEST(RoundEngineTest, ToctouStress) {
  Harness h;
  std::atomic<bool> stop{false};
  std::thread feeder([&] {
    while (!stop.load()) {
      h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
      std::this_thread::sleep_for(200us);
    }
  });

  for (std::uint64_t round = 0; round < 50; ++round) {
    const std::size_t submits_before = h.SubmitCount();
    const std::size_t cancels_before = h.Cancels().size();
    const RoundPlan plan = MakePlan(1000 + round, "10.05", "10.15");
    const RoundResult r = h.engine.RunRound(plan, {200ms, 5ms, 200ms});
    const std::size_t round_submits = h.SubmitCount() - submits_before;

    EXPECT_TRUE(round_submits == 1 || round_submits == 2);
    if (r.outcome == Outcome::kReveal1Timeout) {
      EXPECT_TRUE(round_submits == 1);
    }
    if (round_submits == 2) {
      EXPECT_TRUE(r.outcome != Outcome::kReveal1Timeout);
    }
    EXPECT_TRUE(h.Cancels().size() - cancels_before == 2);
    if (r.t2 != 0) {
      EXPECT_TRUE(r.t2 <= r.t3 && r.t3 < r.submit2_return_ns);
    }
  }
  stop.store(true);
  feeder.join();
}

TEST(RoundEngineTest, HappyPathSell) {
  Harness h(false);
  const RoundPlan plan = MakePlan(20, "10.60", "10.55");
  RoundResult r;
  std::thread runner([&] { r = h.engine.RunRound(plan, kFastTimeouts); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));

  FakeQuotation decoy1;
  decoy1.bids = {PV("10.60", 1)};
  decoy1.asks = {PV("10.40", 1)};
  h.engine.OnQuotation(decoy1);
  std::this_thread::sleep_for(20ms);
  EXPECT_TRUE(h.SubmitCount() == 1);

  FakeQuotation reveal1 = AskQuote({PV("10.40", 2), PV("10.60", 1)});
  reveal1.tp_us = 1'700'000'000'000'333;
  h.engine.OnQuotation(reveal1);
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2; }));

  FakeQuotation decoy2;
  decoy2.bids = {PV("10.55", 1)};
  h.engine.OnQuotation(decoy2);
  std::this_thread::sleep_for(20ms);

  FakeQuotation reveal2 =
      AskQuote({PV("10.40", 2), PV("10.55", 1), PV("10.60", 1)});
  reveal2.tp_us = 1'700'000'000'000'444;
  h.engine.OnQuotation(reveal2);
  runner.join();

  EXPECT_TRUE(r.outcome == Outcome::kOk);
  EXPECT_TRUE(r.t1 < r.t2 && r.t2 <= r.t3 && r.t3 < r.submit2_return_ns);
  EXPECT_TRUE(r.t3 < r.t4);
  EXPECT_TRUE(r.tp1_ns == 1'700'000'000'000'333 * 1000);
  EXPECT_TRUE(r.tp2_ns == 1'700'000'000'000'444 * 1000);
}

TEST(RoundEngineTest, QtyMatchIsFlagNotGate) {
  Harness h;
  const RoundPlan plan = MakePlan(25, "10.05", "10.15");
  RoundResult r;
  std::thread runner([&] { r = h.engine.RunRound(plan, kFastTimeouts); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  h.engine.OnQuotation(BidQuote({PV("10.05", 5)}));
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2; }));
  h.engine.OnQuotation(BidQuote({PV("10.15", 3), PV("10.05", 5)}));
  runner.join();
  EXPECT_TRUE(r.outcome == Outcome::kOk);
  EXPECT_TRUE(!r.qty_match_1 && !r.qty_match_2);
}

TEST(RoundEngineTest, OutcomeNames) {
  const struct {
    Outcome outcome;
    const char* name;
  } cases[] = {
      {Outcome::kOk, "ok"},
      {Outcome::kNoBookSnapshot, "no_book_snapshot"},
      {Outcome::kNoReferencePrice, "no_reference_price"},
      {Outcome::kNoEligiblePrice, "no_eligible_price"},
      {Outcome::kPoolExhausted, "pool_exhausted"},
      {Outcome::kSubmitError1, "submit_error_1"},
      {Outcome::kSubmitError2, "submit_error_2"},
      {Outcome::kAck1Timeout, "ack1_timeout"},
      {Outcome::kAck2Timeout, "ack2_timeout"},
      {Outcome::kReveal1Timeout, "reveal1_timeout"},
      {Outcome::kReveal2Timeout, "reveal2_timeout"},
      {Outcome::kCancelTimeout, "cancel_timeout"},
      {Outcome::kAbortedFill, "aborted_fill"},
      {Outcome::kAbortedError, "aborted_error"},
      {Outcome::kAbortedSignal, "aborted_signal"},
      {Outcome::kSessionEnd, "session_end"},
  };
  for (const auto& c : cases) {
    EXPECT_TRUE(std::string(OutcomeName(c.outcome)) == c.name);
  }
}

TEST(RoundEngineTest, Ack1Timeout) {
  Harness h;
  h.should_ack = [](const std::string&) { return false; };
  const RoundResult r =
      h.engine.RunRound(MakePlan(30, "10.05", "10.15"), {80ms, 100ms, 200ms});
  EXPECT_TRUE(r.outcome == Outcome::kAck1Timeout);
  EXPECT_TRUE(h.SubmitCount() == 1);
  EXPECT_TRUE(h.Cancels().size() == 2);
}

TEST(RoundEngineTest, Ack2Timeout) {
  Harness h;
  h.should_ack = [](const std::string& id) { return id.back() != '2'; };
  const RoundPlan plan = MakePlan(31, "10.05", "10.15");
  RoundResult r;
  std::thread runner(
      [&] { r = h.engine.RunRound(plan, {150ms, 300ms, 200ms}); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  runner.join();
  EXPECT_TRUE(r.outcome == Outcome::kAck2Timeout);
  EXPECT_TRUE(h.SubmitCount() == 2);
  EXPECT_TRUE(r.t2 != 0 && r.t3 != 0 && r.t4 == 0);
  EXPECT_TRUE(h.Cancels().size() == 2);
}

TEST(RoundEngineTest, CancelAckBarrier) {
  Harness h;
  h.auto_cancel_ack = false;
  const RoundPlan plan = MakePlan(40, "10.05", "10.15");
  RoundResult r;
  std::thread runner(
      [&] { r = h.engine.RunRound(plan, {300ms, 400ms, 2000ms}); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2; }));
  h.engine.OnQuotation(BidQuote({PV("10.15", 1), PV("10.05", 1)}));

  EXPECT_TRUE(WaitUntil([&] { return h.Cancels().size() == 1; }));
  h.engine.OnQuotation(BidQuote({PV("10.40", 9)}));
  std::this_thread::sleep_for(50ms);
  EXPECT_TRUE(
      (h.Cancels() == std::vector<std::string>{plan.o1.user_defined_id}));
  const auto snap = h.engine.LatestSnapshot();
  EXPECT_TRUE(snap.has_value() && snap->view.bids.size() == 1 &&
              snap->view.bids[0].volume == 9);

  h.engine.OnCancelAck("unrelated-id", true);
  std::this_thread::sleep_for(50ms);
  EXPECT_TRUE(h.Cancels().size() == 1);
  EXPECT_TRUE(h.engine.unmatched_cancel_acks() == 1);

  h.engine.OnCancelAck(plan.o1.user_defined_id, true);
  EXPECT_TRUE(WaitUntil([&] { return h.Cancels().size() == 2; }));
  EXPECT_TRUE(h.Cancels()[1] == plan.o2.user_defined_id);
  h.engine.OnCancelAck(plan.o2.user_defined_id, true);
  runner.join();
  EXPECT_TRUE(r.outcome == Outcome::kOk);
  EXPECT_TRUE(r.cancel1_ok && r.cancel2_ok);
}

TEST(RoundEngineTest, CancelAckTimeout) {
  Harness h;
  h.auto_cancel_ack = false;
  const RoundPlan plan = MakePlan(41, "10.05", "10.15");
  RoundResult r;
  std::thread runner(
      [&] { r = h.engine.RunRound(plan, {300ms, 400ms, 80ms}); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2; }));
  h.engine.OnQuotation(BidQuote({PV("10.15", 1), PV("10.05", 1)}));
  runner.join();
  EXPECT_TRUE(
      (h.Cancels() == std::vector<std::string>{plan.o1.user_defined_id,
                                               plan.o2.user_defined_id}));
  EXPECT_TRUE(r.outcome == Outcome::kCancelTimeout);
  EXPECT_TRUE(!r.cancel1_ok && !r.cancel2_ok);
}

TEST(RoundEngineTest, SubmitHangRecoversWithinBarrier) {
  Harness h;
  std::atomic<bool> gate{false};
  h.submit_hook = [&](const OrderSpec& s) {
    if (s.user_defined_id.back() == '2') {
      while (!gate.load()) std::this_thread::sleep_for(1ms);
    }
  };
  const RoundPlan plan = MakePlan(50, "10.05", "10.15");
  RoundResult r;
  std::thread runner(
      [&] { r = h.engine.RunRound(plan, {300ms, 50ms, 200ms}); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  std::thread feeder([&] { h.engine.OnQuotation(BidQuote({PV("10.05", 1)})); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2; }));
  std::this_thread::sleep_for(100ms);
  gate.store(true);
  feeder.join();
  runner.join();
  EXPECT_TRUE(r.outcome == Outcome::kReveal2Timeout);
  EXPECT_TRUE(!h.engine.aborted());
  EXPECT_TRUE(!r.o2_submit_in_flight);
  EXPECT_TRUE(r.t2 != 0 && r.t3 != 0);
  EXPECT_TRUE(h.Cancels().size() == 2);
}

TEST(RoundEngineTest, SubmitHangAbortsRun) {
  Harness h;
  std::atomic<bool> gate{false};
  h.submit_hook = [&](const OrderSpec& s) {
    if (s.user_defined_id.back() == '2') {
      while (!gate.load()) std::this_thread::sleep_for(1ms);
    }
  };
  const RoundPlan plan = MakePlan(51, "10.05", "10.15");
  RoundResult r;
  std::thread runner(
      [&] { r = h.engine.RunRound(plan, {150ms, 50ms, 200ms}); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));
  std::thread feeder([&] { h.engine.OnQuotation(BidQuote({PV("10.05", 1)})); });
  runner.join();
  EXPECT_TRUE(r.outcome == Outcome::kAbortedError);
  EXPECT_TRUE(r.o2_submit_in_flight);
  EXPECT_TRUE(h.engine.aborted() &&
              h.engine.abort_reason() == Outcome::kAbortedError);
  EXPECT_TRUE(r.t2 == 0 && r.t3 == 0);
  const RoundResult r2 =
      h.engine.RunRound(MakePlan(52, "10.05", "10.15"), kFastTimeouts);
  EXPECT_TRUE(r2.outcome == Outcome::kAbortedError);
  EXPECT_TRUE(h.SubmitCount() == 2);
  gate.store(true);
  feeder.join();
}

class BlockingQuotation : public FakeQuotation {
 public:
  std::atomic<bool>* entered = nullptr;
  std::atomic<bool>* gate = nullptr;
  const char* GetProductId() const override {
    entered->store(true);
    while (!gate->load()) std::this_thread::sleep_for(1ms);
    return product.c_str();
  }
};

TEST(RoundEngineTest, StaleGenerationCasMustFail) {
  Harness h;

  const RoundPlan plan1 = MakePlan(900, "10.05", "10.15");
  RoundResult r1;
  std::thread runner1(
      [&] { r1 = h.engine.RunRound(plan1, {200ms, 80ms, 200ms}); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 1; }));

  std::atomic<bool> entered{false}, gate{false};
  BlockingQuotation stale;
  stale.bids = {PV("10.05", 1)};
  stale.entered = &entered;
  stale.gate = &gate;
  std::thread stale_thread([&] { h.engine.OnQuotation(stale); });
  EXPECT_TRUE(WaitUntil([&] { return entered.load(); }));

  runner1.join();
  EXPECT_TRUE(r1.outcome == Outcome::kReveal1Timeout);
  EXPECT_TRUE(h.SubmitCount() == 1);

  const RoundPlan plan2 = MakePlan(901, "10.05", "10.15");
  RoundResult r2;
  std::thread runner2([&] { r2 = h.engine.RunRound(plan2, kFastTimeouts); });
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 2; }));

  gate.store(true);
  stale_thread.join();
  std::this_thread::sleep_for(20ms);
  EXPECT_TRUE(h.SubmitCount() == 2);

  h.engine.OnQuotation(BidQuote({PV("10.05", 1)}));
  EXPECT_TRUE(WaitUntil([&] { return h.SubmitCount() == 3; }));
  h.engine.OnQuotation(BidQuote({PV("10.15", 1), PV("10.05", 1)}));
  runner2.join();
  EXPECT_TRUE(r2.outcome == Outcome::kOk);
  EXPECT_TRUE(r2.t1 < r2.t2);
}

QuotationView Book(std::vector<concords_sdk::ticker::PriceVolume> bids,
                   std::vector<concords_sdk::ticker::PriceVolume> asks = {}) {
  QuotationView v;
  v.product_id = "2897";
  v.bids = std::move(bids);
  v.asks = std::move(asks);
  return v;
}

std::vector<Price> Prices(std::initializer_list<const char*> texts) {
  std::vector<Price> out;
  for (const char* t : texts) out.push_back(P(t));
  return out;
}

TEST(PriceSelectionTest, SelectPrices) {
  const SelectionConfig cfg{.min_ticks_below_bid = 2};

  const auto book = Book({PV("10.40", 5), PV("10.30", 2)});
  const auto sel = SelectPrices(Prices({"10.35", "10.30", "10.25", "10.20"}),
                                book, /*is_buy=*/true, cfg);
  EXPECT_TRUE(sel.has_value());
  EXPECT_TRUE(PriceEq(sel->p2, P("10.25")) && PriceEq(sel->p1, P("10.20")));

  const auto full = Book({PV("10.40", 1), PV("10.35", 1), PV("10.30", 1),
                          PV("10.25", 1), PV("10.20", 1)});
  EXPECT_TRUE(
      !SelectPrices(Prices({"10.15", "10.10", "10.05"}), full, true, cfg)
           .has_value());

  const auto gappy = Book({PV("10.40", 1), PV("10.30", 1), PV("10.20", 1),
                           PV("10.10", 1), PV("10.00", 1)});
  const auto sel2 =
      SelectPrices(Prices({"10.25", "10.15", "10.05"}), gappy, true, cfg);
  EXPECT_TRUE(sel2.has_value());
  EXPECT_TRUE(PriceEq(sel2->p2, P("10.25")) && PriceEq(sel2->p1, P("10.15")));

  EXPECT_TRUE(
      !SelectPrices(Prices({"10.45", "10.50"}), book, true, cfg).has_value());

  EXPECT_TRUE(
      !SelectPrices(Prices({"10.05"}), Book({}), true, cfg).has_value());

  EXPECT_TRUE(!SelectPrices(Prices({"10.25"}), book, true, cfg).has_value());

  const auto ask_book = Book({}, {PV("10.40", 3), PV("10.50", 1)});
  const auto sel3 = SelectPrices(Prices({"10.45", "10.50", "10.55", "10.60"}),
                                 ask_book, /*is_buy=*/false, cfg);
  EXPECT_TRUE(sel3.has_value());
  EXPECT_TRUE(PriceEq(sel3->p2, P("10.55")) && PriceEq(sel3->p1, P("10.60")));
}

TEST(PriceSelectionTest, OffsetTicks) {
  EXPECT_TRUE(PriceEq(OffsetTicks(P("10.40"), -2).value(), P("10.30")));
  EXPECT_TRUE(PriceEq(OffsetTicks(P("9.99"), -3).value(), P("9.96")));
  EXPECT_TRUE(PriceEq(OffsetTicks(P("10.40"), 2).value(), P("10.50")));
  EXPECT_TRUE(!OffsetTicks(P("0.02"), -3).has_value());
}

TEST(PricePoolTest, Lifecycle) {
  EXPECT_TRUE(!PricePool::FromStrings({"10.15", "11.27"}).has_value());
  EXPECT_TRUE(!PricePool::FromStrings({"10.15", "10.15"}).has_value());
  EXPECT_TRUE(!PricePool::FromStrings({"10.15", "10.150"}).has_value());

  auto pool = PricePool::FromStrings({"10.15", "10.10", "10.05"});
  EXPECT_TRUE(pool.has_value());
  EXPECT_TRUE(pool->size() == 3 && pool->available_count() == 3);

  EXPECT_TRUE(pool->MarkInUse(P("10.15")));
  EXPECT_TRUE(!pool->MarkInUse(P("10.15")));
  EXPECT_TRUE(pool->available_count() == 2);
  EXPECT_TRUE(pool->Release(P("10.15")));
  EXPECT_TRUE(pool->MarkInUse(P("10.15")));
  EXPECT_TRUE(pool->Release(P("10.15")));
  EXPECT_TRUE(pool->available_count() == 3);

  EXPECT_TRUE(pool->Taint(P("10.10")));
  EXPECT_TRUE(pool->available_count() == 2);
  EXPECT_TRUE(!pool->Release(P("10.10")));
  EXPECT_TRUE(!pool->MarkInUse(P("10.10")));
  EXPECT_TRUE(pool->available_count() == 2);

  EXPECT_TRUE(!pool->MarkInUse(P("9.99")));
  EXPECT_TRUE(pool->TextOf(P("10.05")) != nullptr &&
              *pool->TextOf(P("10.05")) == "10.05");
}

}  // namespace
}  // namespace tick_to_trade
