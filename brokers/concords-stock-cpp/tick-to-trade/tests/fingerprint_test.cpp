#include "../fingerprint.h"

#include <gtest/gtest.h>

#include <vector>

#include "../mono_clock.h"
#include "../quotation_view.h"

namespace tick_to_trade {
namespace {

Price P(const char* s) {
  const std::optional<Price> p = ParsePrice(s);
  EXPECT_TRUE(p.has_value()) << "ParsePrice(\"" << s << "\")";
  return p.value_or(Price{});
}

concords_sdk::ticker::PriceVolume PV(int digits, unsigned int precision,
                                     unsigned int volume) {
  return concords_sdk::ticker::PriceVolume{{digits, precision}, volume};
}

TEST(ParsePriceTest, AcceptsValidPrices) {
  EXPECT_EQ(P("10.15").digits, 1015);
  EXPECT_EQ(P("10.15").precision, 2u);
  EXPECT_EQ(P("10").digits, 10);
  EXPECT_EQ(P("10").precision, 0u);
  EXPECT_EQ(P("9.98").digits, 998);
  EXPECT_EQ(P("0.01").digits, 1);
  EXPECT_EQ(P("0.01").precision, 2u);
  EXPECT_EQ(P("1000").digits, 1000);
}

TEST(ParsePriceTest, RejectsMalformedInput) {
  EXPECT_FALSE(ParsePrice(""));
  EXPECT_FALSE(ParsePrice("."));
  EXPECT_FALSE(ParsePrice("1.2.3"));
  EXPECT_FALSE(ParsePrice("abc"));
  EXPECT_FALSE(ParsePrice("-1"));
  EXPECT_FALSE(ParsePrice("10,5"));
  EXPECT_FALSE(ParsePrice("10.12345"));
  EXPECT_FALSE(ParsePrice("100000000"));
}

TEST(PriceCompareTest, CrossPrecisionEquality) {
  EXPECT_TRUE(PriceEq(P("10.5"), P("10.50")));
  EXPECT_TRUE(PriceEq(P("10.5"), Price{10500, 3}));
  EXPECT_FALSE(PriceEq(P("10.05"), P("10.5")));
  EXPECT_FALSE(PriceEq(P("10.05"), Price{100501, 4}));
}

TEST(PriceCompareTest, Ordering) {
  EXPECT_TRUE(PriceLt(P("10.05"), P("10.5")));
  EXPECT_TRUE(PriceLt(P("9.99"), P("10")));
  EXPECT_FALSE(PriceLt(P("10.50"), P("10.5")));
  EXPECT_FALSE(PriceLt(P("10.5"), P("10.50")));
}

TEST(FromFixedPointTest, BridgesInDomainAndRejectsBadPrecision) {
  EXPECT_TRUE(PriceEq(FromFixedPoint({1050, 2}).value(), P("10.5")));
  EXPECT_TRUE(PriceEq(FromFixedPoint({105000, 4}).value(), P("10.5")));
  EXPECT_FALSE(FromFixedPoint({1050000, 5}).has_value());
  EXPECT_FALSE(FromFixedPoint({105, 4000000000u}).has_value());
}

TEST(TwseTickSizeTest, PriceBands) {
  const auto tick_of = [](const char* price) { return TwseTickSize(P(price)); };
  EXPECT_TRUE(PriceEq(tick_of("9.99").value(), P("0.01")));
  EXPECT_TRUE(PriceEq(tick_of("10.00").value(), P("0.05")));
  EXPECT_TRUE(PriceEq(tick_of("49.95").value(), P("0.05")));
  EXPECT_TRUE(PriceEq(tick_of("50").value(), P("0.1")));
  EXPECT_TRUE(PriceEq(tick_of("99.9").value(), P("0.1")));
  EXPECT_TRUE(PriceEq(tick_of("100").value(), P("0.5")));
  EXPECT_TRUE(PriceEq(tick_of("499.5").value(), P("0.5")));
  EXPECT_TRUE(PriceEq(tick_of("500").value(), P("1")));
  EXPECT_TRUE(PriceEq(tick_of("999").value(), P("1")));
  EXPECT_TRUE(PriceEq(tick_of("1000").value(), P("5")));
}

TEST(TwseTickSizeTest, RejectsNonPositivePrice) {
  EXPECT_FALSE(TwseTickSize(Price{0, 0}).has_value());
  EXPECT_FALSE(TwseTickSize(Price{-100, 2}).has_value());
}

TEST(IsValidTwseTickTest, ValidatesAgainstOwnBand) {
  EXPECT_FALSE(IsValidTwseTick(P("11.27")));
  EXPECT_TRUE(IsValidTwseTick(P("11.25")));
  EXPECT_TRUE(IsValidTwseTick(P("11.3")));
  EXPECT_TRUE(IsValidTwseTick(P("9.99")));
  EXPECT_TRUE(IsValidTwseTick(P("10.05")));
  EXPECT_FALSE(IsValidTwseTick(P("10.02")));
  EXPECT_TRUE(IsValidTwseTick(P("10")));
  EXPECT_TRUE(IsValidTwseTick(P("152.5")));
  EXPECT_FALSE(IsValidTwseTick(P("152.3")));
  EXPECT_TRUE(IsValidTwseTick(P("501")));
  EXPECT_FALSE(IsValidTwseTick(P("501.5")));
  EXPECT_TRUE(IsValidTwseTick(P("1005")));
  EXPECT_FALSE(IsValidTwseTick(P("1001")));
  EXPECT_FALSE(IsValidTwseTick(Price{0, 0}));
}

TEST(LadderScanTest, FindPriceLevel) {
  const std::vector<concords_sdk::ticker::PriceVolume> bids = {
      PV(1040, 2, 12), PV(1035, 2, 3), PV(1030, 2, 7), PV(1025, 2, 1)};

  EXPECT_EQ(FindPriceLevel(bids, P("10.35")), 3u);
  EXPECT_FALSE(FindPriceLevel(bids, P("10.50")).has_value());
  EXPECT_FALSE(FindPriceLevel(bids, P("10.26")).has_value());

  const std::vector<concords_sdk::ticker::PriceVolume> empty;
  EXPECT_FALSE(FindPriceLevel(empty, P("10.35")).has_value());

  const std::vector<concords_sdk::ticker::PriceVolume> coarse = {PV(104, 1, 9)};
  EXPECT_EQ(FindPriceLevel(coarse, P("10.40")), 9u);
}

TEST(LadderScanTest, RankCounting) {
  const std::vector<concords_sdk::ticker::PriceVolume> bids = {
      PV(1040, 2, 12), PV(1035, 2, 3), PV(1030, 2, 7), PV(1025, 2, 1)};

  EXPECT_EQ(CountLevelsAbove(bids, P("10.05")), 4u);
  EXPECT_EQ(CountLevelsAbove(bids, P("10.45")), 0u);
  EXPECT_EQ(CountLevelsAbove(bids, P("10.30")), 2u);
  EXPECT_EQ(CountLevelsBelow(bids, P("10.30")), 1u);

  const std::vector<concords_sdk::ticker::PriceVolume> full = {
      PV(1040, 2, 1), PV(1035, 2, 1), PV(1030, 2, 1), PV(1025, 2, 1),
      PV(1020, 2, 1)};
  EXPECT_EQ(CountLevelsAbove(full, P("10.15")), 5u);
}

TEST(LadderScanTest, SkipsLevelsWithInvalidPrecision) {
  const std::vector<concords_sdk::ticker::PriceVolume> dirty = {
      PV(10400000, 6, 7), PV(1035, 2, 3)};
  EXPECT_FALSE(FindPriceLevel(dirty, P("10.40")).has_value());
  EXPECT_EQ(FindPriceLevel(dirty, P("10.35")), 3u);
  EXPECT_EQ(CountLevelsAbove(dirty, P("10.05")), 1u);
}

TEST(QuotationViewTest, VectorsSatisfyLadderInterface) {
  QuotationView v;
  v.product_id = "2897";
  v.bids = {PV(1040, 2, 2), PV(1030, 2, 5)};
  v.asks = {PV(1045, 2, 1)};
  EXPECT_EQ(FindPriceLevel(v.bids, P("10.30")), 5u);
  EXPECT_EQ(CountLevelsBelow(v.asks, P("10.50")), 1u);
}

TEST(FormatPriceTest, RendersFixedDecimals) {
  EXPECT_EQ(FormatPrice(P("10.15")), "10.15");
  EXPECT_EQ(FormatPrice(P("10")), "10");
  EXPECT_EQ(FormatPrice(P("0.01")), "0.01");
  EXPECT_EQ(FormatPrice(Price{999, 2}), "9.99");
  EXPECT_EQ(FormatPrice(Price{5, 1}), "0.5");
  EXPECT_EQ(FormatPrice(Price{1000, 2}), "10.00");
}

TEST(MonoClockTest, MonotonicNonZero) {
  const std::uint64_t a = MonoNs();
  const std::uint64_t b = MonoNs();
  EXPECT_NE(a, 0u);
  EXPECT_GE(b, a);
  const ClockAnchor anchor = TakeClockAnchor();
  EXPECT_NE(anchor.mono_ns, 0u);
  EXPECT_NE(anchor.realtime_ns, 0u);
}

}  // namespace
}  // namespace tick_to_trade
