#ifndef CONCORDS_SDK_STOCK_ORDER_H_
#define CONCORDS_SDK_STOCK_ORDER_H_

#include <iostream>
#include <optional>
#include <string>
#include <variant>

namespace concords_sdk {
namespace stock {

enum class Market : char { TSE = 'T', OTC = 'O' };
enum class OrderBoard : char {
  RoundLot = '0',
  OddLot = '7',
  PostMarket_Fixed = '3',
  PostMarket_OddLot = '2'
};
enum class FundingType : char {
  Ordinary = '0',
  MarginBuy = '3',
  MarginShortSell = '4',
  SecuritiesBorrowingLending = '6'
};
enum class Side : char { Buy = 'B', Sell = 'S' };
enum class OrderType : char { Market = '1', Limit = '2' };
enum class TimeInForce : char { ROD = '0', IOC = '3', FOK = '4' };
enum class DaytradeShortSell : char { False = 'N', True = 'Y' };
enum class PriceFlag : char { LimitUp = 'U', LimitDown = 'D', Reference = 'C' };

struct OrderInfo {
  enum Market market;                            // 市場別
  enum OrderBoard order_board;                   // 交易盤別
  enum FundingType funding_type;                 // 資金來源別
  std::string symbol;                            // 股票代號
  enum Side side;                                // 買賣方向
  enum OrderType order_type;                     // 委託方式
  enum TimeInForce time_in_force;                // 委託條件
  std::variant<int, std::string> quantity;       // 委託數量
  std::variant<int, double, std::string> price;  // 委託價格
  enum DaytradeShortSell daytrade_shortsell;     // 現股賣出當沖註記
  std::optional<enum PriceFlag> price_flag;      // 價格旗標
  std::string user_defined_id;                   // 使用者自訂義編碼

  OrderInfo() {}
  OrderInfo(enum Market market, enum OrderBoard order_board,
            enum FundingType funding_type, std::string symbol, enum Side side,
            enum OrderType order_type, enum TimeInForce time_in_force,
            std::variant<int, std::string> quantity,
            std::variant<int, double, std::string> price,
            enum DaytradeShortSell daytrade_shortsell,
            std::string user_defined_id)
      : market(market),
        order_board(order_board),
        funding_type(funding_type),
        symbol(symbol),
        side(side),
        order_type(order_type),
        time_in_force(time_in_force),
        quantity(quantity),
        price(price),
        daytrade_shortsell(daytrade_shortsell),
        user_defined_id(user_defined_id) {}

  OrderInfo(enum Market market, enum OrderBoard order_board,
            enum FundingType funding_type, std::string symbol, enum Side side,
            enum OrderType order_type, enum TimeInForce time_in_force,
            std::variant<int, std::string> quantity,
            std::variant<int, double, std::string> price,
            enum DaytradeShortSell daytrade_shortsell,
            std::optional<enum PriceFlag> price_flag,
            std::string user_defined_id)
      : market(market),
        order_board(order_board),
        funding_type(funding_type),
        symbol(symbol),
        side(side),
        order_type(order_type),
        time_in_force(time_in_force),
        quantity(quantity),
        price(price),
        daytrade_shortsell(daytrade_shortsell),
        price_flag(price_flag),
        user_defined_id(user_defined_id) {}
};

struct OrderSubmitResult {
  bool success;
  std::string price;
  std::string effective_quantity;
  std::string timestamp;
  std::string error_message;
  std::string user_defined_id;
  std::string transaction_id;  // 網路單號
};

struct OrderUpdateResult {
  bool success;
  std::string modified_type;
  std::string previous_quantity;
  std::string effective_quantity;
  std::string modified_price;
  std::string timestamp;
  std::string date;
  std::string error_message;
  std::string user_defined_id;
  std::string target_id;  // the order to be modified
  std::string transaction_id;
};

struct OrderCancelResult {
  bool success;
  std::string error_message;
  std::string target_id;  // the order to be deleted
};

struct OrderFillResult {
  std::string side;
  std::string price;
  std::string quantity;
  std::string timestamp;
  std::string user_defined_id;
  std::string transaction_id;
};




}  // namespace stock
}  // namespace concords_sdk

#endif
