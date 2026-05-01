#ifndef CONCORDS_SDK_STOCK_CLIENT_H_
#define CONCORDS_SDK_STOCK_CLIENT_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "account.h"
#include "order.h"

namespace concords_sdk {
namespace stock {

using OrderSubmitCallback = std::function<void(const OrderSubmitResult&)>;
using OrderUpdateCallback = std::function<void(const OrderUpdateResult&)>;
using OrderCancelCallback = std::function<void(const OrderCancelResult&)>;
using OrderFillCallback = std::function<void(const OrderFillResult&)>;

enum class ConnectionState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  RECONNECTING,
  FAILED
};

using ConnectionStateCallback =
    std::function<void(ConnectionState, const std::string& error)>;

class StockClient {
 public:
  virtual ~StockClient() = default;

 public:
  virtual bool Connect() = 0;
  virtual bool ConnectByAccount() = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;
  virtual ConnectionState GetConnectionState() const = 0;
  virtual void SetConnectionStateCallback(ConnectionStateCallback callback) = 0;

  virtual bool Verify() = 0;

  virtual void SubmitOrder(OrderInfo& order_info) = 0;
  virtual void UpdateOrderPrice(
      const std::string& user_defined_id, const std::string& target_id,
      std::variant<int, double, std::string> modified_price) = 0;

  virtual void UpdateOrderQuantity(
      const std::string& user_defined_id, const std::string& target_id,
      std::variant<int, std::string> modified_quantity) = 0;

  virtual void CancelOrder(const std::string& target_id) = 0;

  virtual void SetOrderSubmitCallback(OrderSubmitCallback callback) = 0;
  virtual void SetOrderUpdateCallback(OrderUpdateCallback callback) = 0;
  virtual void SetOrderCancelCallback(OrderCancelCallback callback) = 0;
  virtual void SetOrderFillCallback(OrderFillCallback callback) = 0;

  // Account and position queries
  virtual std::optional<UnrealizedProfitLossStatementResponse>
  GetPosition() = 0;  // GetPosition() now only support get all stocks
  virtual std::optional<TodaySettlementEstimateResponse>
  EstimateTodaySettlement() = 0;
  virtual std::optional<RecentThreeDayNetSettlementResponse>
  GetRecentThreeDaySettlementAmounts() = 0;
  virtual std::optional<HistoricalTransactionStatementResponse> GetTradeHistory(
      const std::string& start_date, const std::string& end_date) = 0;
  virtual std::optional<UnrealizedProfitLossStatementResponse>
  GetUnrealizedProfitLoss() = 0;
  virtual std::optional<RealizedProfitLossStatementResponse>
  GetRealizedProfitLoss(const std::string& start_date,
                        const std::string& end_date) = 0;
  virtual std::optional<MaintenanceRatioResponse> GetMaintenanceRatio() = 0;
  virtual std::optional<TradeFillsResponse> GetTodayTradeFills() = 0;
};

std::unique_ptr<StockClient> BuildStockClient(const char* user_id,
                                              const char* password,
                                              const char* account,
                                              const char* pfx_filepath,
                                              const char* pfx_password);

void Shutdown();

}  // namespace stock
}  // namespace concords_sdk

#endif
