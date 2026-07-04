#ifndef TICK_TO_TRADE_QUOTATION_VIEW_H_
#define TICK_TO_TRADE_QUOTATION_VIEW_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "ticker/include/ticker.h"

namespace tick_to_trade {

struct QuotationView {
  std::string product_id;
  bool is_trial = false;

  std::int64_t tp_ns = 0;
  std::vector<concords_sdk::ticker::PriceVolume> trades;
  std::vector<concords_sdk::ticker::PriceVolume> bids;
  std::vector<concords_sdk::ticker::PriceVolume> asks;
};

inline QuotationView CopyQuotation(const concords_sdk::ticker::Quotation& q) {
  QuotationView v;
  v.product_id = q.GetProductId();
  v.is_trial = q.IsTrial();
  v.tp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                q.GetTimestamp().time_since_epoch())
                .count();
  v.trades.reserve(q.GetTradeSize());
  for (std::size_t i = 0; i < q.GetTradeSize(); ++i) {
    v.trades.push_back(q.GetTrade(i));
  }
  v.bids.reserve(q.GetBidSize());
  for (std::size_t i = 0; i < q.GetBidSize(); ++i) {
    v.bids.push_back(q.GetBid(i));
  }
  v.asks.reserve(q.GetAskSize());
  for (std::size_t i = 0; i < q.GetAskSize(); ++i) {
    v.asks.push_back(q.GetAsk(i));
  }
  return v;
}

struct LiveBidLadder {
  const concords_sdk::ticker::Quotation& quotation;
  std::size_t size() const { return quotation.GetBidSize(); }
  concords_sdk::ticker::PriceVolume operator[](std::size_t i) const {
    return quotation.GetBid(i);
  }
};

struct LiveAskLadder {
  const concords_sdk::ticker::Quotation& quotation;
  std::size_t size() const { return quotation.GetAskSize(); }
  concords_sdk::ticker::PriceVolume operator[](std::size_t i) const {
    return quotation.GetAsk(i);
  }
};

}  // namespace tick_to_trade

#endif  // TICK_TO_TRADE_QUOTATION_VIEW_H_
