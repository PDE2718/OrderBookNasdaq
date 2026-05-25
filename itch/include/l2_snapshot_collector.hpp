#pragma once

#include "l2_snapshot.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace itch {

struct BarAccumulator {
  u64 volume = 0;
  u64 notional = 0;
  u32 open = 0;
  u32 high = 0;
  u32 low = 0;
  u32 close = 0;
  bool has_trade = false;

  void add_trade(u32 price, u32 shares) {
    if (price == 0 || shares == 0) {
      return;
    }
    if (!has_trade) {
      open = price;
      high = price;
      low = price;
      has_trade = true;
    } else {
      high = std::max(high, price);
      low = std::min(low, price);
    }
    close = price;
    volume += shares;
    notional += static_cast<u64>(price) * shares;
  }

  void reset() {
    volume = 0;
    notional = 0;
    open = 0;
    high = 0;
    low = 0;
    close = 0;
    has_trade = false;
  }
};

class BarL2SnapshotCollector {
 public:
  BarL2SnapshotCollector(u64 session_start_ns,
                         u64 session_end_ns,
                         u64 interval_ns)
      : session_start_ns_(session_start_ns),
        session_end_ns_(session_end_ns),
        interval_ns_(interval_ns) {
    if (interval_ns_ == 0) {
      throw std::invalid_argument("interval_ns must be positive");
    }
    if (session_end_ns_ <= session_start_ns_) {
      throw std::invalid_argument("session_end_ns must be greater than session_start_ns");
    }
  }

  u64 session_start_ns() const noexcept { return session_start_ns_; }
  u64 session_end_ns() const noexcept { return session_end_ns_; }
  u64 interval_ns() const noexcept { return interval_ns_; }

  std::size_t bar_count() const {
    const u64 session_ns = session_end_ns_ - session_start_ns_;
    return static_cast<std::size_t>(session_ns / interval_ns_);
  }

  bool in_session(u64 timestamp_ns) const noexcept {
    return timestamp_ns >= session_start_ns_ && timestamp_ns <= session_end_ns_;
  }

  std::size_t bars_ended_before(u64 timestamp_ns) const {
    if (timestamp_ns <= session_start_ns_) {
      return 0;
    }
    const u64 capped = std::min(timestamp_ns - 1, session_end_ns_);
    return std::min(static_cast<std::size_t>((capped - session_start_ns_) / interval_ns_),
                    bar_count());
  }

  std::size_t bars_ended_at_or_before(u64 timestamp_ns) const {
    if (timestamp_ns < session_start_ns_) {
      return 0;
    }
    const u64 capped = std::min(timestamp_ns, session_end_ns_);
    return std::min(static_cast<std::size_t>((capped - session_start_ns_) / interval_ns_),
                    bar_count());
  }

  void add_trade(u32 price, u32 shares) { current_bar_.add_trade(price, shares); }
  void reset_bar() { current_bar_.reset(); }

  const BarAccumulator& current_bar() const noexcept { return current_bar_; }

  BarSnapshotView capture(u64 interval_start_ns,
                          u64 interval_end_ns,
                          L2SnapshotView book) const {
    return BarSnapshotView{
        interval_start_ns,
        interval_end_ns,
        current_bar_.volume,
        current_bar_.notional,
        current_bar_.open,
        current_bar_.high,
        current_bar_.low,
        current_bar_.close,
        current_bar_.has_trade,
        book};
  }

 private:
  u64 session_start_ns_ = 0;
  u64 session_end_ns_ = 0;
  u64 interval_ns_ = 0;
  BarAccumulator current_bar_;
};

}  // namespace itch
