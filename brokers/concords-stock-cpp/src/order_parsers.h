#pragma once

#include <iostream>
#include <string>

#include "stock-client/order.h"

namespace concordsapi {
namespace stockclient {

inline Market parseMarket(const std::string& str) {
  if (str == "TSE") return Market::TSE;
  if (str == "OTC") return Market::OTC;
  std::cerr << "Invalid market: " << str << std::endl;
  exit(1);
}

inline OrderBoard parseOrderBoard(const std::string& str) {
  if (str == "RoundLot") return OrderBoard::RoundLot;
  if (str == "OddLot") return OrderBoard::OddLot;
  if (str == "PostMarket_Fixed") return OrderBoard::PostMarket_Fixed;
  if (str == "PostMarket_OddLot") return OrderBoard::PostMarket_OddLot;
  std::cerr << "Invalid order_board: " << str << std::endl;
  exit(1);
}

inline FundingType parseFundingType(const std::string& str) {
  if (str == "Cash") return FundingType::Cash;
  if (str == "MarginBuy") return FundingType::MarginBuy;
  if (str == "MarginShortSell") return FundingType::MarginShortSell;
  std::cerr << "Invalid funding_type: " << str << std::endl;
  exit(1);
}

inline Side parseSide(const std::string& str) {
  if (str == "Buy" || str == "B" || str == "BUY") return Side::Buy;
  if (str == "Sell" || str == "S" || str == "SELL") return Side::Sell;
  std::cerr << "Invalid side: " << str << std::endl;
  exit(1);
}

inline OrderType parseOrderType(const std::string& str) {
  if (str == "Limit") return OrderType::Limit;
  if (str == "Market") return OrderType::Market;
  std::cerr << "Invalid order_type: " << str << std::endl;
  exit(1);
}

inline TimeInForce parseTimeInForce(const std::string& str) {
  if (str == "ROD") return TimeInForce::ROD;
  if (str == "IOC") return TimeInForce::IOC;
  if (str == "FOK") return TimeInForce::FOK;
  std::cerr << "Invalid time_in_force: " << str << std::endl;
  exit(1);
}

inline DaytradeShortSell parseDaytradeShortSell(const std::string& str) {
  if (str == "True" || str == "Y") return DaytradeShortSell::True;
  if (str == "False" || str == "N") return DaytradeShortSell::False;
  std::cerr << "Invalid daytrade_shortsell: " << str << std::endl;
  exit(1);
}

}  // namespace stockclient
}  // namespace concordsapi
