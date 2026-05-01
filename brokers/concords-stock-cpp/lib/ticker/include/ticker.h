#ifndef CONCORDS_SDK_TICKER_H_
#define CONCORDS_SDK_TICKER_H_

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <memory_resource>
#include <string>

namespace concords_sdk {
namespace ticker {

enum class Exchange { kTWSE, kTPEX, kTFX, kOTC };

struct FixedPoint {
  int digits;
  unsigned int precision;
};

struct PriceVolume {
  FixedPoint price;
  unsigned int volume;
};

class Quotation {
 public:
  virtual ~Quotation() = default;

 public:
  virtual std::chrono::time_point<std::chrono::system_clock> GetTimestamp()
      const = 0;
  virtual const char* GetProductId() const = 0;
  virtual Exchange GetExchange() const = 0;

  virtual std::size_t GetTradeSize() const = 0;
  virtual PriceVolume GetTrade(std::size_t) const = 0;
  virtual bool IsTrial() const = 0;

  virtual std::size_t GetBidSize() const = 0;
  virtual PriceVolume GetBid(std::size_t) const = 0;

  virtual std::size_t GetAskSize() const = 0;
  virtual PriceVolume GetAsk(std::size_t) const = 0;
};

struct QuotationDeleter {
  QuotationDeleter() {}
  QuotationDeleter(std::pmr::memory_resource* memory_resource,
                   std::size_t memory_footprint)
      : memory_resource{memory_resource}, memory_footprint{memory_footprint} {}

  void operator()(Quotation* quotation) const {
    quotation->~Quotation();
    memory_resource->deallocate(quotation, memory_footprint);
  }

  std::pmr::memory_resource* memory_resource;
  std::size_t memory_footprint;
};

class Ticker {
 public:
  virtual ~Ticker() = default;

 public:
  // return false if maximum subscription limit is reached
  virtual bool Subscribe(const char* product_id) = 0;
  virtual bool Unsubscribe(const char* product_id) = 0;

  // return nullptr if loss quotation is detected
  virtual const std::unique_ptr<Quotation, QuotationDeleter>
  NextQuotation() = 0;

  virtual void SetQuotationCallback(
      std::function<void(const Quotation&)> callback) = 0;
  virtual void ClearQuotationCallback() = 0;

  virtual void SetErrorCallback(
      std::function<void(const std::string&)> callback) = 0;
  virtual void ClearErrorCallback() = 0;
};

std::unique_ptr<Ticker> BuildTicker(const char* user_id, const char* password,
                                    const char* pfx_filepath,
                                    std::pmr::memory_resource* memory_resource =
                                        std::pmr::get_default_resource());

}  // namespace ticker
}  // namespace concords_sdk

#endif
