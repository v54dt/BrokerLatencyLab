#ifndef TICK_TO_TRADE_PRICE_SELECTION_H_
#define TICK_TO_TRADE_PRICE_SELECTION_H_

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "fingerprint.h"
#include "quotation_view.h"

namespace tick_to_trade {
struct SelectionConfig {
  int min_ticks_below_bid = 2;
};

struct SelectedPrices {
  Price p1, p2;
};

inline std::optional<Price> OffsetTicks(Price ref, int n) {
  const std::optional<Price> tick = TwseTickSize(ref);
  if (!tick) return std::nullopt;
  const std::uint32_t p = std::max(ref.precision, tick->precision);
  const std::int64_t digits =
      ref.digits * Pow10(p - ref.precision) +
      static_cast<std::int64_t>(n) * tick->digits * Pow10(p - tick->precision);
  if (digits <= 0) return std::nullopt;
  return Price{digits, p};
}

inline std::optional<Price> BestTouch(const QuotationView& snapshot,
                                      bool is_buy) {
  const auto& ladder = is_buy ? snapshot.bids : snapshot.asks;
  std::optional<Price> best;
  for (const concords_sdk::ticker::PriceVolume& pv : ladder) {
    const std::optional<Price> p = FromFixedPoint(pv.price);
    if (!p) continue;
    if (!best || (is_buy ? PriceLt(*best, *p) : PriceLt(*p, *best))) best = *p;
  }
  return best;
}

inline std::optional<SelectedPrices> SelectPrices(std::vector<Price> available,
                                                  const QuotationView& snapshot,
                                                  bool is_buy,
                                                  const SelectionConfig& cfg) {
  const std::optional<Price> best = BestTouch(snapshot, is_buy);
  if (!best) return std::nullopt;

  const auto& ladder = is_buy ? snapshot.bids : snapshot.asks;

  auto farther = [is_buy](Price a, Price b) {
    return is_buy ? PriceLt(a, b) : PriceLt(b, a);
  };

  const std::optional<Price> p2_limit = OffsetTicks(
      *best, is_buy ? -cfg.min_ticks_below_bid : cfg.min_ticks_below_bid);
  if (!p2_limit) return std::nullopt;

  auto eligible = [&](Price p) {
    if (!farther(p, *best)) return false;
    if (FindPriceLevel(ladder, p)) return false;
    const std::size_t rank_above =
        is_buy ? CountLevelsAbove(ladder, p) : CountLevelsBelow(ladder, p);
    return rank_above <= 4;
  };

  std::sort(available.begin(), available.end(),
            [&](Price a, Price b) { return farther(b, a); });

  std::optional<Price> p2, p1;
  for (const Price& p : available) {
    if (!eligible(p)) continue;
    if (!p2) {
      if (farther(*p2_limit, p)) continue;
      p2 = p;
      continue;
    }
    if (farther(p, *p2)) {
      p1 = p;
      break;
    }
  }
  if (!p1 || !p2) return std::nullopt;
  return SelectedPrices{*p1, *p2};
}

class PricePool {
 public:
  enum class State { kAvailable, kInUse, kTainted };

  struct Entry {
    Price price;
    std::string text;
    State state = State::kAvailable;
  };

  static std::optional<PricePool> FromStrings(
      const std::vector<std::string>& texts) {
    PricePool pool;
    for (const std::string& t : texts) {
      const std::optional<Price> p = ParsePrice(t);
      if (!p || !IsValidTwseTick(*p)) return std::nullopt;
      for (const Entry& e : pool.entries_) {
        if (PriceEq(e.price, *p)) return std::nullopt;
      }
      pool.entries_.push_back(Entry{*p, t, State::kAvailable});
    }
    return pool;
  }

  std::vector<Price> Available() const {
    std::vector<Price> out;
    for (const Entry& e : entries_) {
      if (e.state == State::kAvailable) out.push_back(e.price);
    }
    return out;
  }

  bool MarkInUse(Price p) {
    Entry* e = Find(p);
    if (!e || e->state != State::kAvailable) return false;
    e->state = State::kInUse;
    return true;
  }
  bool Release(Price p) { return SetState(p, State::kAvailable); }
  bool Taint(Price p) { return SetState(p, State::kTainted); }

  const std::string* TextOf(Price p) const {
    for (const Entry& e : entries_) {
      if (PriceEq(e.price, p)) return &e.text;
    }
    return nullptr;
  }

  std::size_t size() const { return entries_.size(); }
  std::size_t available_count() const { return Available().size(); }

 private:
  Entry* Find(Price p) {
    for (Entry& e : entries_) {
      if (PriceEq(e.price, p)) return &e;
    }
    return nullptr;
  }

  bool SetState(Price p, State s) {
    Entry* e = Find(p);
    if (!e) return false;
    if (e->state == State::kTainted) return false;
    e->state = s;
    return true;
  }

  std::vector<Entry> entries_;
};

}  // namespace tick_to_trade

#endif
