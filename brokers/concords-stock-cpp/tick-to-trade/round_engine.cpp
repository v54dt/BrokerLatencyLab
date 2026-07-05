#include "round_engine.h"

#include <cstring>

#include "mono_clock.h"

namespace tick_to_trade {
namespace {
constexpr std::chrono::milliseconds kPostRaceGrace{200};

std::int64_t TpNs(const concords_sdk::ticker::Quotation& q) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             q.GetTimestamp().time_since_epoch())
      .count();
}

}  // namespace

const char* OutcomeName(Outcome outcome) {
  switch (outcome) {
    case Outcome::kOk:
      return "ok";
    case Outcome::kNoBookSnapshot:
      return "no_book_snapshot";
    case Outcome::kNoReferencePrice:
      return "no_reference_price";
    case Outcome::kNoEligiblePrice:
      return "no_eligible_price";
    case Outcome::kPoolExhausted:
      return "pool_exhausted";
    case Outcome::kSubmitError1:
      return "submit_error_1";
    case Outcome::kSubmitError2:
      return "submit_error_2";
    case Outcome::kAck1Timeout:
      return "ack1_timeout";
    case Outcome::kAck2Timeout:
      return "ack2_timeout";
    case Outcome::kReveal1Timeout:
      return "reveal1_timeout";
    case Outcome::kReveal2Timeout:
      return "reveal2_timeout";
    case Outcome::kCancelTimeout:
      return "cancel_timeout";
    case Outcome::kAbortedFill:
      return "aborted_fill";
    case Outcome::kAbortedError:
      return "aborted_error";
    case Outcome::kAbortedSignal:
      return "aborted_signal";
    case Outcome::kSessionEnd:
      return "session_end";
  }
  return "unknown";
}

RoundEngine::RoundEngine(RunConfig config, Deps deps)
    : config_(std::move(config)), deps_(std::move(deps)) {}

void RoundEngine::OnQuotation(const concords_sdk::ticker::Quotation& q) {
  const std::uint64_t ts = MonoNs();

  const std::uint32_t s = state_.load(std::memory_order_acquire);
  const RoundPhase phase = PhaseOf(s);

  if (std::strcmp(q.GetProductId(), config_.symbol.c_str()) != 0) return;
  const bool trial = q.IsTrial();

  if (phase == RoundPhase::kWaitReveal1) {
    if (trial) {
      saw_trial_.store(true, std::memory_order_relaxed);
      return;
    }
    const Price p1 = UnpackPrice(p1_packed_.load(std::memory_order_relaxed));
    std::optional<unsigned int> hit;
    if (config_.is_buy) {
      hit = FindPriceLevel(LiveBidLadder{q}, p1);
    } else {
      hit = FindPriceLevel(LiveAskLadder{q}, p1);
    }
    if (!hit) return;
    std::uint32_t expected = s;
    if (!state_.compare_exchange_strong(
            expected, WithPhase(s, RoundPhase::kSubmitting2) | kO2FiredBit,
            std::memory_order_acq_rel)) {
      return;
    }
    t3_.store(MonoNs(), std::memory_order_relaxed);
    deps_.submit_order(o2_spec_);
    submit2_return_ns_.store(MonoNs(), std::memory_order_relaxed);
    t2_.store(ts, std::memory_order_relaxed);
    tp1_ns_.store(TpNs(q), std::memory_order_relaxed);
    qty_match_1_.store(*hit == static_cast<unsigned int>(qty1_),
                       std::memory_order_relaxed);
    std::uint32_t submitting =
        WithPhase(s, RoundPhase::kSubmitting2) | kO2FiredBit;
    state_.compare_exchange_strong(
        submitting, WithPhase(submitting, RoundPhase::kWaitReveal2),
        std::memory_order_acq_rel);
    {
      std::lock_guard<std::mutex> lock(mu_);
      reveal1_done_ = true;
    }
    cv_.notify_all();
    return;
  }

  if (phase == RoundPhase::kWaitReveal2) {
    if (trial) {
      saw_trial_.store(true, std::memory_order_relaxed);
      return;
    }
    const Price p2 = UnpackPrice(p2_packed_.load(std::memory_order_relaxed));
    std::optional<unsigned int> hit;
    if (config_.is_buy) {
      hit = FindPriceLevel(LiveBidLadder{q}, p2);
    } else {
      hit = FindPriceLevel(LiveAskLadder{q}, p2);
    }
    if (!hit) return;
    std::uint32_t expected = s;
    if (!state_.compare_exchange_strong(expected,
                                        WithPhase(s, RoundPhase::kDone),
                                        std::memory_order_acq_rel)) {
      return;
    }
    t4_.store(ts, std::memory_order_relaxed);
    tp2_ns_.store(TpNs(q), std::memory_order_relaxed);
    qty_match_2_.store(*hit == static_cast<unsigned int>(qty2_),
                       std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lock(mu_);
      reveal2_done_ = true;
    }
    cv_.notify_all();
    return;
  }

  if (trial) {
    if (phase != RoundPhase::kIdle) {
      saw_trial_.store(true, std::memory_order_relaxed);
    }
    return;
  }

  if (phase == RoundPhase::kIdle || phase == RoundPhase::kDone ||
      phase == RoundPhase::kAbandoned) {
    QuotationView view = CopyQuotation(q);
    const std::uint64_t now = MonoNs();
    {
      std::lock_guard<std::mutex> lock(snapshot_mu_);
      latest_snapshot_ = std::move(view);
      snapshot_mono_ns_ = now;
    }
    snapshot_cv_.notify_all();
  }
}

void RoundEngine::OnSubmitAck(const std::string& user_defined_id, bool success,
                              const std::string& broker_timestamp) {
  const std::uint64_t now = MonoNs();
  bool matched = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (user_defined_id == o1_id_) {
      ack1_seen_ = true;
      ack1_ok_ = success;
      ack1_local_ns_ = now;
      ack1_broker_ts_ = broker_timestamp;
      matched = true;
    } else if (user_defined_id == o2_id_) {
      ack2_seen_ = true;
      ack2_ok_ = success;
      ack2_local_ns_ = now;
      ack2_broker_ts_ = broker_timestamp;
      matched = true;
    }
  }
  if (!matched) return;
  if (!success) AbandonActive();
  cv_.notify_all();
}

void RoundEngine::OnCancelAck(const std::string& target_id, bool success) {
  bool matched = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (target_id == o1_id_) {
      cancel1_seen_ = true;
      cancel1_ok_ = success;
      matched = true;
    } else if (target_id == o2_id_) {
      cancel2_seen_ = true;
      cancel2_ok_ = success;
      matched = true;
    }
  }
  if (matched) {
    cv_.notify_all();
  } else {
    unmatched_cancel_acks_.fetch_add(1, std::memory_order_relaxed);
  }
}

void RoundEngine::OnFill() { RequestAbort(Outcome::kAbortedFill); }

void RoundEngine::RequestAbort(Outcome reason) {
  Outcome expected = Outcome::kOk;
  abort_reason_.compare_exchange_strong(expected, reason,
                                        std::memory_order_acq_rel);
  AbandonActive();
  { std::lock_guard<std::mutex> lock(mu_); }
  cv_.notify_all();
  { std::lock_guard<std::mutex> lock(snapshot_mu_); }
  snapshot_cv_.notify_all();
}

bool RoundEngine::TryAbandon(RoundPhase expected) {
  std::uint32_t s = state_.load(std::memory_order_acquire);
  if (PhaseOf(s) != expected) return false;
  return state_.compare_exchange_strong(s, WithPhase(s, RoundPhase::kAbandoned),
                                        std::memory_order_acq_rel);
}

void RoundEngine::AbandonActive() {
  std::uint32_t s = state_.load(std::memory_order_acquire);
  while (PhaseOf(s) == RoundPhase::kWaitReveal1 ||
         PhaseOf(s) == RoundPhase::kSubmitting2 ||
         PhaseOf(s) == RoundPhase::kWaitReveal2) {
    if (state_.compare_exchange_weak(s, WithPhase(s, RoundPhase::kAbandoned),
                                     std::memory_order_acq_rel)) {
      return;
    }
  }
}

bool RoundEngine::WaitForSnapshot(std::chrono::milliseconds timeout,
                                  std::uint64_t min_mono_ns) {
  auto fresh = [&] {
    return latest_snapshot_.has_value() && snapshot_mono_ns_ >= min_mono_ns;
  };
  std::unique_lock<std::mutex> lock(snapshot_mu_);
  return snapshot_cv_.wait_for(lock, timeout, [&] {
    return fresh() || aborted();
  }) && fresh();
}

std::optional<RoundEngine::StampedSnapshot> RoundEngine::LatestSnapshot()
    const {
  std::lock_guard<std::mutex> lock(snapshot_mu_);
  if (!latest_snapshot_) return std::nullopt;
  return StampedSnapshot{*latest_snapshot_, snapshot_mono_ns_};
}

bool RoundEngine::WaitBookClear(const std::vector<Price>& prices,
                                std::chrono::milliseconds timeout,
                                std::uint64_t min_mono_ns) {
  auto clear = [&]() -> bool {
    if (!latest_snapshot_ || snapshot_mono_ns_ < min_mono_ns) return false;
    const auto& ladder =
        config_.is_buy ? latest_snapshot_->bids : latest_snapshot_->asks;
    for (const Price& p : prices) {
      if (FindPriceLevel(ladder, p)) return false;
    }
    return true;
  };
  std::unique_lock<std::mutex> lock(snapshot_mu_);
  return snapshot_cv_.wait_for(lock, timeout,
                               [&] { return clear() || aborted(); }) &&
         !aborted() && clear();
}

bool RoundEngine::WaitFlag(bool& flag, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mu_);
  return cv_.wait_for(lock, timeout, [&] { return flag || aborted(); }) && flag;
}

bool RoundEngine::WaitFlagNoAbort(bool& flag,
                                  std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mu_);
  return cv_.wait_for(lock, timeout, [&] { return flag; });
}

RoundResult RoundEngine::RunRound(const RoundPlan& plan,
                                  const StageTimeouts& timeouts) {
  RoundResult result;
  result.round_id = plan.round_id;
  result.o1_id = plan.o1.user_defined_id;
  result.o2_id = plan.o2.user_defined_id;
  result.price1_text = plan.o1.price_text;
  result.price2_text = plan.o2.price_text;
  result.quantity = plan.o1.quantity;

  if (aborted()) {
    result.outcome = abort_reason();
    return result;
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    reveal1_done_ = reveal2_done_ = false;
    ack1_seen_ = ack2_seen_ = false;
    ack1_ok_ = ack2_ok_ = false;
    ack1_local_ns_ = ack2_local_ns_ = 0;
    ack1_broker_ts_.clear();
    ack2_broker_ts_.clear();
    cancel1_seen_ = cancel2_seen_ = false;
    cancel1_ok_ = cancel2_ok_ = false;
    o1_id_ = plan.o1.user_defined_id;
    o2_id_ = plan.o2.user_defined_id;
  }
  t2_.store(0, std::memory_order_relaxed);
  t3_.store(0, std::memory_order_relaxed);
  t4_.store(0, std::memory_order_relaxed);
  submit2_return_ns_.store(0, std::memory_order_relaxed);
  tp1_ns_.store(0, std::memory_order_relaxed);
  tp2_ns_.store(0, std::memory_order_relaxed);
  qty_match_1_.store(false, std::memory_order_relaxed);
  qty_match_2_.store(false, std::memory_order_relaxed);
  saw_trial_.store(false, std::memory_order_relaxed);
  p1_packed_.store(PackPrice(plan.o1.price), std::memory_order_relaxed);
  p2_packed_.store(PackPrice(plan.o2.price), std::memory_order_relaxed);
  qty1_ = plan.o1.quantity;
  qty2_ = plan.o2.quantity;
  o2_spec_ = plan.o2;

  {
    std::lock_guard<std::mutex> lock(snapshot_mu_);
    latest_snapshot_.reset();
    snapshot_mono_ns_ = 0;
  }

  const std::uint32_t prev = state_.load(std::memory_order_relaxed);
  state_.store(PackState(GenOf(prev) + 1, RoundPhase::kWaitReveal1),
               std::memory_order_release);

  result.t1 = MonoNs();
  deps_.submit_order(plan.o1);
  result.submit1_return_ns = MonoNs();

  if (!WaitFlag(ack1_seen_, timeouts.ack)) {
    result.outcome = aborted() ? abort_reason() : Outcome::kAck1Timeout;
    Finalize(plan, timeouts, result);
    return result;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ack1_ok_) {
      result.outcome = Outcome::kSubmitError1;
    }
  }
  if (result.outcome == Outcome::kSubmitError1 || aborted()) {
    if (aborted()) result.outcome = abort_reason();
    Finalize(plan, timeouts, result);
    return result;
  }

  if (!WaitFlag(reveal1_done_, timeouts.reveal)) {
    if (aborted()) {
      result.outcome = abort_reason();
      Finalize(plan, timeouts, result);
      return result;
    }
    if (TryAbandon(RoundPhase::kWaitReveal1)) {
      result.outcome = Outcome::kReveal1Timeout;
      Finalize(plan, timeouts, result);
      return result;
    }
    if (!WaitFlagNoAbort(reveal1_done_, kPostRaceGrace)) {
      result.outcome = Outcome::kReveal1Timeout;
      Finalize(plan, timeouts, result);
      return result;
    }
  }

  if (!WaitFlag(ack2_seen_, timeouts.ack)) {
    result.outcome = aborted() ? abort_reason() : Outcome::kAck2Timeout;
    Finalize(plan, timeouts, result);
    return result;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ack2_ok_) result.outcome = Outcome::kSubmitError2;
  }
  if (result.outcome == Outcome::kSubmitError2 || aborted()) {
    if (aborted()) result.outcome = abort_reason();
    Finalize(plan, timeouts, result);
    return result;
  }

  if (!WaitFlag(reveal2_done_, timeouts.reveal)) {
    if (aborted()) {
      result.outcome = abort_reason();
      Finalize(plan, timeouts, result);
      return result;
    }
    if (TryAbandon(RoundPhase::kWaitReveal2)) {
      result.outcome = Outcome::kReveal2Timeout;
      Finalize(plan, timeouts, result);
      return result;
    }
    if (!WaitFlagNoAbort(reveal2_done_, kPostRaceGrace)) {
      result.outcome = Outcome::kReveal2Timeout;
      Finalize(plan, timeouts, result);
      return result;
    }
  }

  result.outcome = Outcome::kOk;
  Finalize(plan, timeouts, result);
  return result;
}

void RoundEngine::CancelBoth(const RoundPlan& plan,
                             const StageTimeouts& timeouts,
                             RoundResult& result) {
  deps_.cancel_order(plan.o1.user_defined_id);
  const bool c1 = WaitFlagNoAbort(cancel1_seen_, timeouts.cancel);
  deps_.cancel_order(plan.o2.user_defined_id);
  const bool c2 = WaitFlagNoAbort(cancel2_seen_, timeouts.cancel);
  {
    std::lock_guard<std::mutex> lock(mu_);
    result.cancel1_ok = cancel1_ok_;
    result.cancel2_ok = cancel2_ok_;
  }
  if ((!c1 || !c2) && result.outcome == Outcome::kOk) {
    result.outcome = Outcome::kCancelTimeout;
  }
}

void RoundEngine::Finalize(const RoundPlan& plan, const StageTimeouts& timeouts,
                           RoundResult& result) {
  AbandonActive();

  bool fields_trustworthy = true;
  const std::uint32_t s_after = state_.load(std::memory_order_acquire);
  if (O2FiredOf(s_after)) {
    if (!WaitFlagNoAbort(reveal1_done_, timeouts.ack)) {
      RequestAbort(Outcome::kAbortedError);
      result.o2_submit_in_flight = true;
      fields_trustworthy = false;
    }
  }

  CancelBoth(plan, timeouts, result);

  if (fields_trustworthy) {
    result.t2 = t2_.load(std::memory_order_relaxed);
    result.t3 = t3_.load(std::memory_order_relaxed);
    result.t4 = t4_.load(std::memory_order_relaxed);
    result.submit2_return_ns =
        submit2_return_ns_.load(std::memory_order_relaxed);
    result.tp1_ns = tp1_ns_.load(std::memory_order_relaxed);
    result.tp2_ns = tp2_ns_.load(std::memory_order_relaxed);
    result.qty_match_1 = qty_match_1_.load(std::memory_order_relaxed);
    result.qty_match_2 = qty_match_2_.load(std::memory_order_relaxed);
  }
  result.saw_trial = saw_trial_.load(std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(mu_);
    result.ack1_local_ns = ack1_local_ns_;
    result.ack2_local_ns = ack2_local_ns_;
    result.ack1_ok = ack1_ok_;
    result.ack2_ok = ack2_ok_;
    result.ack1_broker_ts = ack1_broker_ts_;
    result.ack2_broker_ts = ack2_broker_ts_;
  }
  if (aborted()) result.outcome = abort_reason();
  const std::uint32_t s = state_.load(std::memory_order_relaxed);
  state_.store(WithPhase(s, RoundPhase::kIdle), std::memory_order_release);
}

}  // namespace tick_to_trade
