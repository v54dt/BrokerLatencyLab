#ifndef TICK_TO_TRADE_ROUND_ENGINE_H_
#define TICK_TO_TRADE_ROUND_ENGINE_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "fingerprint.h"
#include "quotation_view.h"
#include "ticker/include/ticker.h"

namespace tick_to_trade {
enum class RoundPhase : std::uint8_t {
  kIdle,
  kWaitReveal1,
  kSubmitting2,
  kWaitReveal2,
  kDone,
  kAbandoned,
};

enum class Outcome : std::uint8_t {
  kOk,
  kNoBookSnapshot,
  kNoReferencePrice,
  kNoEligiblePrice,
  kPoolExhausted,
  kSubmitError1,
  kSubmitError2,
  kAck1Timeout,
  kAck2Timeout,
  kReveal1Timeout,
  kReveal2Timeout,
  kCancelTimeout,
  kAbortedFill,
  kAbortedError,
  kAbortedSignal,
  kSessionEnd,
};

const char* OutcomeName(Outcome outcome);

struct OrderSpec {
  std::string user_defined_id;
  std::string price_text;
  Price price;
  int quantity = 0;
};

struct RoundPlan {
  std::uint64_t round_id = 0;
  OrderSpec o1;
  OrderSpec o2;
};

struct StageTimeouts {
  std::chrono::milliseconds ack{10'000};
  std::chrono::milliseconds reveal{10'000};
  std::chrono::milliseconds cancel{10'000};
};

struct RoundResult {
  std::uint64_t round_id = 0;
  Outcome outcome = Outcome::kOk;

  bool o2_submit_in_flight = false;

  std::string o1_id, o2_id;
  std::string price1_text, price2_text;
  int quantity = 0;
  std::uint64_t t1 = 0, t2 = 0, t3 = 0, t4 = 0;

  std::uint64_t submit1_return_ns = 0, submit2_return_ns = 0;
  std::uint64_t ack1_local_ns = 0, ack2_local_ns = 0;
  bool ack1_ok = false, ack2_ok = false;
  std::string ack1_broker_ts, ack2_broker_ts;
  std::int64_t tp1_ns = 0, tp2_ns = 0;
  bool qty_match_1 = false, qty_match_2 = false;
  bool saw_trial = false;
  bool cancel1_ok = false, cancel2_ok = false;
};

class RoundEngine {
 public:
  struct Deps {
    std::function<void(const OrderSpec&)> submit_order;
    std::function<void(const std::string& target_id)> cancel_order;
  };

  struct RunConfig {
    std::string symbol;
    bool is_buy = true;
  };

  RoundEngine(RunConfig config, Deps deps);

  void OnQuotation(const concords_sdk::ticker::Quotation& quotation);
  void OnSubmitAck(const std::string& user_defined_id, bool success,
                   const std::string& broker_timestamp);
  void OnCancelAck(const std::string& target_id, bool success);
  void OnFill();
  void RequestAbort(Outcome reason);

  bool aborted() const {
    return abort_reason_.load(std::memory_order_acquire) != Outcome::kOk;
  }
  Outcome abort_reason() const {
    return abort_reason_.load(std::memory_order_acquire);
  }

  struct StampedSnapshot {
    QuotationView view;
    std::uint64_t mono_ns = 0;
  };
  bool WaitForSnapshot(std::chrono::milliseconds timeout,
                       std::uint64_t min_mono_ns = 0);
  std::optional<StampedSnapshot> LatestSnapshot() const;
  bool WaitBookClear(const std::vector<Price>& prices,
                     std::chrono::milliseconds timeout,
                     std::uint64_t min_mono_ns = 0);

  std::uint64_t unmatched_cancel_acks() const {
    return unmatched_cancel_acks_.load(std::memory_order_relaxed);
  }

  RoundResult RunRound(const RoundPlan& plan, const StageTimeouts& timeouts);

 private:
  static constexpr std::uint32_t kO2FiredBit = 0x8u;
  static constexpr std::uint32_t PackState(std::uint32_t gen, RoundPhase p) {
    return (gen << 4) | static_cast<std::uint32_t>(p);
  }
  static constexpr RoundPhase PhaseOf(std::uint32_t s) {
    return static_cast<RoundPhase>(s & 0x7u);
  }
  static constexpr bool O2FiredOf(std::uint32_t s) {
    return (s & kO2FiredBit) != 0;
  }
  static constexpr std::uint32_t GenOf(std::uint32_t s) { return s >> 4; }
  static constexpr std::uint32_t WithPhase(std::uint32_t s, RoundPhase p) {
    return (s & ~0x7u) | static_cast<std::uint32_t>(p);
  }

  bool TryAbandon(RoundPhase expected);
  void AbandonActive();
  void CancelBoth(const RoundPlan& plan, const StageTimeouts& timeouts,
                  RoundResult& result);
  void Finalize(const RoundPlan& plan, const StageTimeouts& timeouts,
                RoundResult& result);
  bool WaitFlag(bool& flag, std::chrono::milliseconds timeout);
  bool WaitFlagNoAbort(bool& flag, std::chrono::milliseconds timeout);

  const RunConfig config_;
  Deps deps_;

  std::atomic<std::uint32_t> state_{PackState(0, RoundPhase::kIdle)};

  static constexpr std::uint64_t PackPrice(Price p) {
    return (static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(static_cast<std::int32_t>(p.digits)))
            << 8) |
           p.precision;
  }
  static constexpr Price UnpackPrice(std::uint64_t v) {
    return Price{static_cast<std::int32_t>(static_cast<std::uint32_t>(v >> 8)),
                 static_cast<std::uint32_t>(v & 0xffu)};
  }
  std::atomic<std::uint64_t> p1_packed_{0}, p2_packed_{0};

  int qty1_ = 0, qty2_ = 0;
  OrderSpec o2_spec_;

  std::atomic<std::uint64_t> t2_{0}, t3_{0}, t4_{0}, submit2_return_ns_{0};
  std::atomic<std::int64_t> tp1_ns_{0}, tp2_ns_{0};
  std::atomic<bool> qty_match_1_{false}, qty_match_2_{false};

  std::atomic<bool> saw_trial_{false};
  std::atomic<std::uint64_t> unmatched_cancel_acks_{0};
  std::atomic<Outcome> abort_reason_{Outcome::kOk};

  mutable std::mutex mu_;
  std::condition_variable cv_;
  bool reveal1_done_ = false, reveal2_done_ = false;
  bool ack1_seen_ = false, ack2_seen_ = false;
  bool ack1_ok_ = false, ack2_ok_ = false;
  std::uint64_t ack1_local_ns_ = 0, ack2_local_ns_ = 0;
  std::string ack1_broker_ts_, ack2_broker_ts_;
  bool cancel1_seen_ = false, cancel2_seen_ = false;
  bool cancel1_ok_ = false, cancel2_ok_ = false;
  std::string o1_id_, o2_id_;

  mutable std::mutex snapshot_mu_;
  std::condition_variable snapshot_cv_;
  std::optional<QuotationView> latest_snapshot_;
  std::uint64_t snapshot_mono_ns_ = 0;
};

}  // namespace tick_to_trade

#endif
