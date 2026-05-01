#ifndef CONCORDS_SDK_STOCK_ACCOUNT_H_
#define CONCORDS_SDK_STOCK_ACCOUNT_H_

#include <string>
#include <vector>

namespace concords_sdk {
namespace stock {

struct Account {
  std::string account_type;      // 市場別
  std::string user_id;           // 身分證字號
  std::string user_name;         // 客戶姓名
  std::string branch_number;     // 分公司碼
  std::string branch_name;       // 分公司名稱
  std::string user_account;      // 客戶帳號
  std::string smart_order;       // 智慧下單
  std::string daytrade_enabled;  // 現沖戶
};

// 證券及時交割計算
struct TodaySettlementEstimate {
  std::string transaction_date;  // 交易日期
  std::string symbol;
  std::string symbol_name;
  std::string funding_type;  // 委託別: 普通 融資 融券 借券
  std::string side;          // 買進或賣出
  std::string price;
  std::string quantity;
  std::string value;
  std::string fee;
  std::string tax;
  std::string margin;
  std::string interest;
  std::string net_settlement;  // 淨收付
};

// 近3日應收付
struct RecentThreeDayNetSettlement {
  std::string transaction_date;   // 交易日
  std::string settlement_date;    // 交割日
  std::string receivable_amount;  // 應收金額
  std::string payable_amount;     // 應付金額
};

// 證券歷史對帳單
struct HistoricalTransactionStatement {
  std::string market_type;
  std::string transaction_date;
  std::string funding_type;  // 委託別: 普通 融資 融券 借券
  std::string side;          // 買進或賣出
  std::string symbol;
  std::string symbol_name;
  std::string price;
  std::string quantity;
  std::string value;
  std::string fee;
  std::string tax;
  std::string margin;
  std::string interest;
  std::string net_settlement;
};

// 已實現損益
struct RealizedProfitLossStatement {
  std::string market_type;
  std::string symbol;
  std::string symbol_name;
  std::string transaction_date;
  std::string funding_type;
  std::string side;
  std::string price;
  std::string quantity;
  std::string net_proceeds;  // 帳面收入(淨可得金額)
  std::string investment_cost;
  std::string adjusted_investment_cost;
  std::string profit_loss;
  std::string return_on_investment;
};

// 未實現損益
struct UnrealizedProfitLossStatement {
  std::string market_type;
  std::string symbol;
  std::string symbol_name;
  std::string funding_type;  // 委託別: 普通 融資 融券 借券
  std::string quantity;
  std::string average_cost;
  std::string investment_cost;  // 平均成本
  std::string market_price;     // 現價
  std::string market_value;     // 市值
  std::string profit_loss;
  std::string return_on_investment;
};

// 維持率查詢
struct MaintenanceRatio {
  std::string market_type;   // 市場別
  std::string symbol;        // 股票代號
  std::string symbol_name;   // 股票名稱
  std::string funding_type;  // 委託別
  std::string price;
  std::string quantity;           // 庫存股數
  std::string average_cost;       // 成交均價
  std::string collateral;         // 自備款/擔保品
  std::string margin;             // 融資金/保證金
  std::string interest;           // 利息
  std::string market_price;       // 現價
  std::string market_value;       // 市值
  std::string maintenance_ratio;  // 維持率
};

// 當日成交回報明細
struct TradeFills {
  std::string market_type;
  std::string order_board;   // 交易盤別
  std::string funding_type;  // 委託別: 普通 融資 融券 借券
  std::string symbol;
  std::string symbol_name;
  std::string price;
  std::string quantity;
  std::string daytrade_shortsell;  // 是否是現股當沖賣出
  std::string timestamp;
};

struct AccountResponse {
  bool status;
  std::string error_message;
  std::vector<Account> responses;
};

struct TodaySettlementEstimateResponse {
  bool status;
  std::string error_message;
  std::vector<TodaySettlementEstimate> responses;
};

struct RecentThreeDayNetSettlementResponse {
  bool status;
  std::string error_message;
  std::vector<RecentThreeDayNetSettlement> responses;
};

struct HistoricalTransactionStatementResponse {
  bool status;
  std::string error_message;
  std::vector<HistoricalTransactionStatement> responses;
};

struct RealizedProfitLossStatementResponse {
  bool status;
  std::string error_message;
  std::vector<RealizedProfitLossStatement> responses;
};

struct UnrealizedProfitLossStatementResponse {
  bool status;
  std::string error_message;
  std::vector<UnrealizedProfitLossStatement> responses;
};

struct MaintenanceRatioResponse {
  bool status;
  std::string error_message;
  std::vector<MaintenanceRatio> responses;
};

struct TradeFillsResponse {
  bool status;
  std::string error_message;
  std::vector<TradeFills> responses;
};

}  // namespace stock
}  // namespace concords_sdk

#endif
