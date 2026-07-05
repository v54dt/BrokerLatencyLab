#include <toml++/toml.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "sdk_includes.h"

namespace {

using namespace std::chrono_literals;

constexpr std::chrono::seconds kCancelAckTimeout{5};

const char* StatusName(concords_sdk::stock::OrderStatus status) {
  using concords_sdk::stock::OrderStatus;
  switch (status) {
    case OrderStatus::Submitted:
      return "submitted";
    case OrderStatus::PartialFilled:
      return "partial_filled";
    case OrderStatus::Filled:
      return "filled";
    case OrderStatus::Cancelled:
      return "cancelled";
    case OrderStatus::Failed:
      return "failed";
    case OrderStatus::SubmitPending:
      return "submit_pending";
    case OrderStatus::PreOrderSubmitted:
      return "preorder_submitted";
    case OrderStatus::PreOrderCancelled:
      return "preorder_cancelled";
    case OrderStatus::PreOrderFailed:
      return "preorder_failed";
  }
  return "unknown";
}

bool IsOpen(concords_sdk::stock::OrderStatus status) {
  using concords_sdk::stock::OrderStatus;
  return status == OrderStatus::Submitted ||
         status == OrderStatus::PartialFilled ||
         status == OrderStatus::SubmitPending ||
         status == OrderStatus::PreOrderSubmitted;
}

struct CancelWaiter {
  std::mutex mu;
  std::condition_variable cv;
  std::string target;
  bool seen = false;
  bool ok = false;
  std::string error;

  void Arm(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu);
    target = id;
    seen = false;
    ok = false;
    error.clear();
  }

  bool Wait() {
    std::unique_lock<std::mutex> lock(mu);
    return cv.wait_for(lock, kCancelAckTimeout, [&] { return seen; }) && ok;
  }
};

bool TryCancel(concords_sdk::stock::StockClient& client, CancelWaiter& waiter,
               const std::string& id, const char* id_kind) {
  if (id.empty()) return false;
  waiter.Arm(id);
  client.CancelOrder(id);
  const bool ok = waiter.Wait();
  std::printf("  cancel via %s=%s: %s%s%s\n", id_kind, id.c_str(),
              ok ? "ok" : "failed", waiter.error.empty() ? "" : " - ",
              waiter.error.c_str());
  return ok;
}

int Usage(int code) {
  std::fprintf(stderr,
               "usage: cancel_all [--cancel-all] [-y] [--config <path>]\n"
               "  default: list orders only\n"
               "  --cancel-all: cancel every open order (asks unless -y)\n");
  return code;
}

}  // namespace

int main(int argc, char** argv) {
  bool do_cancel = false;
  bool assume_yes = false;
  std::string config_path = "config.toml";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--cancel-all") {
      do_cancel = true;
    } else if (arg == "-y" || arg == "--yes") {
      assume_yes = true;
    } else if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      return Usage(0);
    } else {
      return Usage(2);
    }
  }

  toml::table config;
  try {
    config = toml::parse_file(config_path);
  } catch (const toml::parse_error& err) {
    std::fprintf(stderr, "failed to parse %s: %s\n", config_path.c_str(),
                 err.what());
    return 1;
  }
  const auto user_key = [&](const char* key) -> std::string {
    const auto value = config["user"][key].value<std::string>();
    if (!value) {
      std::fprintf(stderr, "missing config key: user.%s\n", key);
      std::exit(1);
    }
    return *value;
  };
  const std::string user_id = user_key("user_id");
  const std::string password = user_key("password");
  const std::string account = user_key("account");
  const std::string pfx_filepath = user_key("pfx_filepath");
  const std::string pfx_password = user_key("pfx_password");

  const std::unique_ptr<concords_sdk::stock::StockClient> client =
      concords_sdk::stock::BuildStockClient(
          user_id.c_str(), password.c_str(), account.c_str(),
          pfx_filepath.c_str(), pfx_password.c_str());

  CancelWaiter waiter;
  client->SetOrderCancelCallback(
      [&](const concords_sdk::stock::OrderCancelResult& result) {
        std::lock_guard<std::mutex> lock(waiter.mu);
        if (result.target_id != waiter.target) {
          std::printf("  unmatched cancel ack: target_id=%s success=%d\n",
                      result.target_id.c_str(), result.success);
          return;
        }
        waiter.seen = true;
        waiter.ok = result.success;
        waiter.error = result.error_message;
        waiter.cv.notify_all();
      });

  if (!client->Connect()) {
    std::fprintf(stderr, "failed to connect\n");
    return 1;
  }

  const std::optional<concords_sdk::stock::OrderStateResponse> orders =
      client->GetOrders();
  if (!orders || !orders->status) {
    std::fprintf(stderr, "GetOrders failed: %s\n",
                 orders ? orders->error_message.c_str() : "no response");
    client->Disconnect();
    return 1;
  }

  std::vector<concords_sdk::stock::OrderState> open;
  std::printf("%-14s %-14s %-6s %-4s %10s %6s %-18s %6s %6s\n", "order_id",
              "ticket_id", "symbol", "side", "price", "qty", "status", "filled",
              "remain");
  for (const concords_sdk::stock::OrderState& o : orders->responses) {
    std::printf("%-14s %-14s %-6s %-4c %10s %6s %-18s %6s %6s\n",
                o.order_id.c_str(), o.order_ticket_id.c_str(), o.symbol.c_str(),
                static_cast<char>(o.side), o.price.c_str(), o.quantity.c_str(),
                StatusName(o.order_status), o.filled_quantity.c_str(),
                o.remaining_quantity.c_str());
    if (IsOpen(o.order_status)) open.push_back(o);
  }
  std::printf("%zu orders, %zu open\n", orders->responses.size(), open.size());

  if (!do_cancel || open.empty()) {
    client->Disconnect();
    return 0;
  }

  if (!assume_yes) {
    std::printf("cancel %zu open orders? [y/N] ", open.size());
    std::fflush(stdout);
    const int c = std::getchar();
    if (c != 'y' && c != 'Y') {
      std::printf("aborted\n");
      client->Disconnect();
      return 0;
    }
  }

  int failed = 0;
  for (const concords_sdk::stock::OrderState& o : open) {
    std::printf("cancelling %s (%s %s x%s)\n", o.order_id.c_str(),
                o.symbol.c_str(), o.price.c_str(), o.quantity.c_str());
    // order_id first; broker id semantics unverified, fall back to ticket id.
    if (!TryCancel(*client, waiter, o.order_id, "order_id") &&
        !TryCancel(*client, waiter, o.order_ticket_id, "order_ticket_id")) {
      ++failed;
    }
  }
  std::printf("done: %zu attempted, %d failed\n", open.size(), failed);

  client->Disconnect();
  return failed == 0 ? 0 : 1;
}
