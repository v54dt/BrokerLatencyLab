#pragma once

#include <iostream>
#include <string>

#include "fubon.hpp"

namespace fubon_parsers {

inline fubon::BsAction parseSide(const std::string& s) {
  if (s == "BUY" || s == "Buy" || s == "B") return fubon::BsAction::BUY;
  if (s == "SELL" || s == "Sell" || s == "S") return fubon::BsAction::SELL;
  std::cerr << "Invalid side: " << s << std::endl;
  exit(1);
}

inline fubon::MarketType parseMarketType(const std::string& s) {
  if (s == "COMMON") return fubon::MarketType::COMMON;
  if (s == "FIXING") return fubon::MarketType::FIXING;
  if (s == "ODD") return fubon::MarketType::ODD;
  if (s == "INTRADAY_ODD") return fubon::MarketType::INTRADAY_ODD;
  if (s == "EMG") return fubon::MarketType::EMG;
  if (s == "EMG_ODD") return fubon::MarketType::EMG_ODD;
  std::cerr << "Invalid market_type: " << s << std::endl;
  exit(1);
}

inline fubon::PriceType parsePriceType(const std::string& s) {
  if (s == "LIMIT") return fubon::PriceType::LIMIT;
  if (s == "MARKET") return fubon::PriceType::MARKET;
  if (s == "LIMIT_UP") return fubon::PriceType::LIMIT_UP;
  if (s == "LIMIT_DOWN") return fubon::PriceType::LIMIT_DOWN;
  if (s == "REFERENCE") return fubon::PriceType::REFERENCE;
  std::cerr << "Invalid price_type: " << s << std::endl;
  exit(1);
}

inline fubon::TimeInForce parseTimeInForce(const std::string& s) {
  if (s == "ROD") return fubon::TimeInForce::ROD;
  if (s == "IOC") return fubon::TimeInForce::IOC;
  if (s == "FOK") return fubon::TimeInForce::FOK;
  std::cerr << "Invalid time_in_force: " << s << std::endl;
  exit(1);
}

inline fubon::OrderType parseOrderType(const std::string& s) {
  if (s == "STOCK") return fubon::OrderType::STOCK;
  if (s == "MARGIN") return fubon::OrderType::MARGIN;
  if (s == "SHORT") return fubon::OrderType::SHORT;
  if (s == "SBL") return fubon::OrderType::SBL;
  if (s == "DAY_TRADE") return fubon::OrderType::DAY_TRADE;
  std::cerr << "Invalid order_type: " << s << std::endl;
  exit(1);
}

}  // namespace fubon_parsers
