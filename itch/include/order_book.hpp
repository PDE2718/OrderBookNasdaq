#pragma once

#include "l2_snapshot.hpp"
#include "messages.hpp"
#include "object_pool.hpp"
#include "types.hpp"
#include "unordered_dense.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
#include <span>
#include <variant>

namespace itch {

class Level;

enum class BookSide {
  Buy,
  Sell,
};

enum class ApplyStatus {
  Ignored,
  Applied,
  DuplicateOrder,
  UnknownOrder,
  InvalidSide,
  InvalidQuantity,
  StockLocateMismatch,
};

struct ApplyResult {
  ApplyStatus status = ApplyStatus::Ignored;
  char message_type = '\0';
  u64 order_reference_number = 0;

  bool ok() const {
    return status == ApplyStatus::Applied || status == ApplyStatus::Ignored;
  }
};

struct BookStats {
  std::size_t orders = 0;
  std::size_t bid_levels = 0;
  std::size_t ask_levels = 0;
  u64 bid_shares = 0;
  u64 ask_shares = 0;
  u64 visible_executed_shares = 0;
  u32 last_executed_price = 0;
  u32 last_executed_shares = 0;
  std::uint32_t order_pool_capacity = 0;
  std::uint32_t order_pool_high_watermark = 0;
  std::uint32_t order_pool_expansions = 0;
  std::uint32_t level_pool_capacity = 0;
  std::uint32_t level_pool_high_watermark = 0;
  std::uint32_t level_pool_expansions = 0;
};

struct BookCapacity {
  std::uint32_t initial_orders = 4'096;
  std::uint32_t initial_levels = 512;
};

struct BookOrder {
  u64 reference_number = 0;
  u64 timestamp = 0;
  u32 shares = 0;
  u32 price = 0;
  BookSide side = BookSide::Buy;
  Level* level = nullptr;
  BookOrder* prev = nullptr;
  BookOrder* next = nullptr;
};

class Level {
 public:
  explicit Level(u32 price = 0) : price_(price) {}

  u32 price() const { return price_; }
  u64 total_shares() const { return total_shares_; }
  u32 order_count() const { return order_count_; }
  bool empty() const { return order_count_ == 0; }
  const BookOrder* front() const { return first_; }
  const BookOrder* back() const { return last_; }

  void push_back(BookOrder* order) {
    order->level = this;
    order->prev = last_;
    order->next = nullptr;
    if (last_ != nullptr) {
      last_->next = order;
    } else {
      first_ = order;
    }
    last_ = order;
    total_shares_ += order->shares;
    ++order_count_;
  }

  void reduce(BookOrder* order, u32 shares) {
    const u32 reduced = std::min(order->shares, shares);
    order->shares -= reduced;
    total_shares_ -= reduced;
  }

  void remove(BookOrder* order) {
    if (order->prev != nullptr) {
      order->prev->next = order->next;
    } else {
      first_ = order->next;
    }

    if (order->next != nullptr) {
      order->next->prev = order->prev;
    } else {
      last_ = order->prev;
    }

    total_shares_ -= order->shares;
    --order_count_;
    order->level = nullptr;
    order->prev = nullptr;
    order->next = nullptr;
  }

 private:
  u32 price_ = 0;
  u64 total_shares_ = 0;
  u32 order_count_ = 0;
  BookOrder* first_ = nullptr;
  BookOrder* last_ = nullptr;
};

class LimitOrderBook {
 public:
  explicit LimitOrderBook(u16 stock_locate = 0,
                          BookCapacity capacity = BookCapacity{});

  u16 stock_locate() const { return stock_locate_; }
  void set_stock_locate(u16 stock_locate) { stock_locate_ = stock_locate; }

  ApplyResult apply(const BookMessage& message);
  ApplyResult apply(const AddOrder& message);
  ApplyResult apply(const AddOrderWithMPID& message);
  ApplyResult apply(const OrderExecuted& message);
  ApplyResult apply(const OrderExecutedWithPrice& message);
  ApplyResult apply(const OrderCancel& message);
  ApplyResult apply(const OrderDelete& message);
  ApplyResult apply(const OrderReplace& message);

  u32 best_bid() const;
  u32 best_ask() const;
  u64 bid_shares_at(u32 price) const;
  u64 ask_shares_at(u32 price) const;
  u32 bid_order_count_at(u32 price) const;
  u32 ask_order_count_at(u32 price) const;
  const Level* bid_level(u32 price) const;
  const Level* ask_level(u32 price) const;
  const BookOrder* find_order(u64 reference_number) const;
  BookStats stats() const;
  void log_watermark(std::ostream& out, const char* label = nullptr) const;
  std::size_t fill_bid_levels(std::span<LevelSnapshot> out) const;
  std::size_t fill_ask_levels(std::span<LevelSnapshot> out) const;

 private:
  using OrderIndex = ankerl::unordered_dense::map<u64, BookOrder*>;
  using BidLevels = std::map<u32, Level*, std::greater<u32>>;
  using AskLevels = std::map<u32, Level*>;

  struct AddFields {
    u64 reference_number = 0;
    u64 timestamp = 0;
    u32 shares = 0;
    u32 price = 0;
    u16 stock_locate = 0;
    char side = '\0';
    char type = '\0';
  };

  ApplyResult add_order(const AddFields& fields);
  ApplyResult execute_order(u64 reference_number,
                            u32 shares,
                            u32 execution_price,
                            bool has_execution_price,
                            char message_type);
  ApplyResult cancel_order(u64 reference_number, u32 shares, char message_type);
  ApplyResult delete_order(u64 reference_number, char message_type);
  ApplyResult reduce_order(BookOrder* order, u32 shares, char message_type);
  ApplyResult remove_order(BookOrder* order, char message_type);
  ApplyResult check_stock_locate(u16 stock_locate, char message_type, u64 ref) const;

  Level* get_or_create_level(BookSide side, u32 price);
  void erase_level_if_empty(BookSide side, Level* level);
  const Level* find_level(BookSide side, u32 price) const;
  static std::optional<BookSide> parse_side(char side);

  u16 stock_locate_ = 0;
  OrderIndex orders_;
  BidLevels bids_;
  AskLevels asks_;
  ObjectPool<BookOrder> order_pool_;
  ObjectPool<Level> level_pool_;
  u64 bid_shares_ = 0;
  u64 ask_shares_ = 0;
  u64 visible_executed_shares_ = 0;
  u32 last_executed_price_ = 0;
  u32 last_executed_shares_ = 0;
};

}  // namespace itch
