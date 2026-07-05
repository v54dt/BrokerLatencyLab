#ifndef TICK_TO_TRADE_PRICE_SELECTION_H_
#define TICK_TO_TRADE_PRICE_SELECTION_H_

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

#include "fingerprint.h"
#include "quotation_view.h"

namespace tick_to_trade {

struct SelectionConfig {
  int min_ticks_from_touch = 2;
  int max_ticks_from_touch = 10;
};

struct SelectedPrices {
  Price p1, p2;
};

// TWSE band boundaries paired with the tick of the band below them:
// stepping down from exactly 10.00 moves by 0.01, not 0.05.
inline std::optional<Price> NextTickBelow(Price p) {
  static constexpr struct {
    Price at;
    Price tick_below;
  } kBoundaries[] = {
      {{10, 0}, {1, 2}},  {{50, 0}, {5, 2}},   {{100, 0}, {1, 1}},
      {{500, 0}, {5, 1}}, {{1000, 0}, {1, 0}},
  };
  std::optional<Price> tick = TwseTickSize(p);
  if (!tick) return std::nullopt;
  for (const auto& b : kBoundaries) {
    if (PriceEq(p, b.at)) {
      tick = b.tick_below;
      break;
    }
  }
  const std::uint32_t prec = std::max(p.precision, tick->precision);
  const std::int64_t digits = p.digits * Pow10(prec - p.precision) -
                              tick->digits * Pow10(prec - tick->precision);
  if (digits <= 0) return std::nullopt;
  return Price{digits, prec};
}

inline std::optional<Price> NextTickAbove(Price p) {
  const std::optional<Price> tick = TwseTickSize(p);
  if (!tick) return std::nullopt;
  const std::uint32_t prec = std::max(p.precision, tick->precision);
  return Price{p.digits * Pow10(prec - p.precision) +
                   tick->digits * Pow10(prec - tick->precision),
               prec};
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

// Walks outward from the touch, one valid tick at a time, and picks the two
// nearest eligible levels: p2 first (>= min_ticks_from_touch away), then p1
// one or more eligible steps further out. Eligible = not excluded (in-use or
// tainted), not an occupied level, and still inside the 5-level display
// window once inserted.
inline std::optional<SelectedPrices> SelectPricesFromBook(
    const QuotationView& snapshot, bool is_buy, const SelectionConfig& cfg,
    const std::function<bool(Price)>& excluded = nullptr) {
  const std::optional<Price> best = BestTouch(snapshot, is_buy);
  if (!best) return std::nullopt;

  const auto& ladder = is_buy ? snapshot.bids : snapshot.asks;
  const auto eligible = [&](Price p) {
    if (excluded && excluded(p)) return false;
    if (FindPriceLevel(ladder, p)) return false;
    const std::size_t rank =
        is_buy ? CountLevelsAbove(ladder, p) : CountLevelsBelow(ladder, p);
    return rank <= 4;
  };

  std::optional<Price> p2, p1;
  std::optional<Price> cursor = *best;
  for (int k = 1; k <= cfg.max_ticks_from_touch; ++k) {
    cursor = is_buy ? NextTickBelow(*cursor) : NextTickAbove(*cursor);
    if (!cursor) break;
    if (k < cfg.min_ticks_from_touch || !eligible(*cursor)) continue;
    if (!p2) {
      p2 = *cursor;
    } else {
      p1 = *cursor;
      break;
    }
  }
  if (!p1 || !p2) return std::nullopt;
  return SelectedPrices{*p1, *p2};
}

// Tracks prices with a live or unknown-state order. Tainted prices (cancel
// failed or timed out) stay blocked for the whole run.
class PriceTracker {
 public:
  bool MarkInUse(Price p) {
    if (IsBlocked(p)) return false;
    in_use_.push_back(p);
    return true;
  }

  bool Release(Price p) {
    const auto it = Find(in_use_, p);
    if (it == in_use_.end()) return false;
    in_use_.erase(it);
    return true;
  }

  void Taint(Price p) {
    const auto it = Find(in_use_, p);
    if (it != in_use_.end()) in_use_.erase(it);
    if (Find(tainted_, p) == tainted_.end()) tainted_.push_back(p);
  }

  bool IsBlocked(Price p) const {
    return Find(in_use_, p) != in_use_.end() ||
           Find(tainted_, p) != tainted_.end();
  }

  std::size_t in_use_count() const { return in_use_.size(); }
  std::size_t tainted_count() const { return tainted_.size(); }

 private:
  static std::vector<Price>::const_iterator Find(const std::vector<Price>& v,
                                                 Price p) {
    return std::find_if(v.begin(), v.end(),
                        [&](Price e) { return PriceEq(e, p); });
  }
  static std::vector<Price>::iterator Find(std::vector<Price>& v, Price p) {
    return std::find_if(v.begin(), v.end(),
                        [&](Price e) { return PriceEq(e, p); });
  }

  std::vector<Price> in_use_;
  std::vector<Price> tainted_;
};

}  // namespace tick_to_trade

#endif  // TICK_TO_TRADE_PRICE_SELECTION_H_
