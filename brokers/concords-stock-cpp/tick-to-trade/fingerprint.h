#ifndef TICK_TO_TRADE_FINGERPRINT_H_
#define TICK_TO_TRADE_FINGERPRINT_H_

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ticker/include/ticker.h"

namespace tick_to_trade {

struct Price {
  std::int64_t digits = 0;
  std::uint32_t precision = 0;
};

inline constexpr std::int64_t Pow10(std::uint32_t n) {
  std::int64_t v = 1;
  while (n--) v *= 10;
  return v;
}

inline std::optional<Price> ParsePrice(std::string_view s) {
  if (s.empty()) return std::nullopt;
  std::int64_t digits = 0;
  std::uint32_t precision = 0;
  bool seen_dot = false;
  bool seen_digit = false;
  for (char c : s) {
    if (c == '.') {
      if (seen_dot) return std::nullopt;
      seen_dot = true;
      continue;
    }
    if (c < '0' || c > '9') return std::nullopt;
    seen_digit = true;
    if (seen_dot && ++precision > 4) return std::nullopt;
    digits = digits * 10 + (c - '0');
    if (digits >= 100'000'000) return std::nullopt;
  }
  if (!seen_digit) return std::nullopt;
  return Price{digits, precision};
}

inline bool PriceEq(Price a, Price b) {
  return a.digits * Pow10(b.precision) == b.digits * Pow10(a.precision);
}

inline bool PriceLt(Price a, Price b) {
  return a.digits * Pow10(b.precision) < b.digits * Pow10(a.precision);
}

inline std::string FormatPrice(Price p) {
  std::string digits = std::to_string(p.digits);
  if (p.precision == 0) return digits;
  while (digits.size() <= p.precision) digits.insert(0, "0");
  digits.insert(digits.size() - p.precision, ".");
  return digits;
}

inline std::optional<Price> FromFixedPoint(
    concords_sdk::ticker::FixedPoint fp) {
  if (fp.precision > 4) return std::nullopt;
  return Price{fp.digits, fp.precision};
}

inline std::optional<Price> TwseTickSize(Price price) {
  if (price.digits <= 0) return std::nullopt;
  if (PriceLt(price, Price{10, 0})) return Price{1, 2};
  if (PriceLt(price, Price{50, 0})) return Price{5, 2};
  if (PriceLt(price, Price{100, 0})) return Price{1, 1};
  if (PriceLt(price, Price{500, 0})) return Price{5, 1};
  if (PriceLt(price, Price{1000, 0})) return Price{1, 0};
  return Price{5, 0};
}

inline bool IsValidTwseTick(Price price) {
  const std::optional<Price> tick = TwseTickSize(price);
  if (!tick) return false;
  const std::uint32_t p = std::max(price.precision, tick->precision);
  const std::int64_t price_scaled = price.digits * Pow10(p - price.precision);
  const std::int64_t tick_scaled = tick->digits * Pow10(p - tick->precision);
  return price_scaled % tick_scaled == 0;
}

template <typename Ladder>
inline std::optional<unsigned int> FindPriceLevel(const Ladder& ladder,
                                                  Price target) {
  for (std::size_t i = 0; i < ladder.size(); ++i) {
    const concords_sdk::ticker::PriceVolume pv = ladder[i];
    const std::optional<Price> level = FromFixedPoint(pv.price);
    if (level && PriceEq(*level, target)) return pv.volume;
  }
  return std::nullopt;
}

template <typename Ladder>
inline std::size_t CountLevelsAbove(const Ladder& ladder, Price target) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < ladder.size(); ++i) {
    const concords_sdk::ticker::PriceVolume pv = ladder[i];
    const std::optional<Price> level = FromFixedPoint(pv.price);
    if (level && PriceLt(target, *level)) ++n;
  }
  return n;
}

template <typename Ladder>
inline std::size_t CountLevelsBelow(const Ladder& ladder, Price target) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < ladder.size(); ++i) {
    const concords_sdk::ticker::PriceVolume pv = ladder[i];
    const std::optional<Price> level = FromFixedPoint(pv.price);
    if (level && PriceLt(*level, target)) ++n;
  }
  return n;
}

}  // namespace tick_to_trade

#endif  // TICK_TO_TRADE_FINGERPRINT_H_
