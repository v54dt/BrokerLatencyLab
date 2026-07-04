#ifndef TICK_TO_TRADE_MONO_CLOCK_H_
#define TICK_TO_TRADE_MONO_CLOCK_H_

#include <cstdint>
#include <ctime>

namespace tick_to_trade {

inline std::uint64_t MonoNs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

inline std::uint64_t RealtimeNs() {
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

struct ClockAnchor {
  std::uint64_t mono_ns;
  std::uint64_t realtime_ns;
};

inline ClockAnchor TakeClockAnchor() {
  return ClockAnchor{MonoNs(), RealtimeNs()};
}

}  // namespace tick_to_trade

#endif  // TICK_TO_TRADE_MONO_CLOCK_H_
