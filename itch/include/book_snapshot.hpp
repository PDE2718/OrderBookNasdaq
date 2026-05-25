#pragma once

#include "l2_snapshot.hpp"
#include "order_book.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace itch {

class BookSnapshotBuffer {
 public:
  explicit BookSnapshotBuffer(std::size_t depth)
      : bid_levels_(depth), ask_levels_(depth) {
    if (depth == 0) {
      throw std::invalid_argument("depth must be positive");
    }
  }

  std::size_t depth() const noexcept { return bid_levels_.size(); }

  L2SnapshotView capture(const LimitOrderBook& book) {
    const std::size_t bid_count = book.fill_bid_levels(
        std::span<LevelSnapshot>(bid_levels_.data(), bid_levels_.size()));
    const std::size_t ask_count = book.fill_ask_levels(
        std::span<LevelSnapshot>(ask_levels_.data(), ask_levels_.size()));
    return L2SnapshotView{book.best_bid(),
                          book.best_ask(),
                          bid_levels_.data(),
                          bid_count,
                          ask_levels_.data(),
                          ask_count};
  }

 private:
  std::vector<LevelSnapshot> bid_levels_;
  std::vector<LevelSnapshot> ask_levels_;
};

}  // namespace itch
