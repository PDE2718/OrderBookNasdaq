#pragma once

#include "types.hpp"

#include <cstddef>

namespace itch {

struct LevelSnapshot {
  u32 price = 0;
  u64 shares = 0;
  u32 orders = 0;
};

struct L2SnapshotView {
  u32 best_bid = 0;
  u32 best_ask = 0;
  const LevelSnapshot* bid_levels = nullptr;
  std::size_t bid_level_count = 0;
  const LevelSnapshot* ask_levels = nullptr;
  std::size_t ask_level_count = 0;
};

struct BarSnapshotView {
  u64 interval_start_ns = 0;
  u64 interval_end_ns = 0;
  u64 volume = 0;
  u64 notional = 0;
  u32 open = 0;
  u32 high = 0;
  u32 low = 0;
  u32 close = 0;
  bool has_trade = false;
  L2SnapshotView book;
};

}  // namespace itch
