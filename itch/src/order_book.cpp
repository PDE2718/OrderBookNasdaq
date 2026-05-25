#include "order_book.hpp"

#include <ostream>

namespace itch {
namespace {

template <typename Levels>
std::size_t fill_levels(const Levels& levels, std::span<LevelSnapshot> out) {
  std::size_t count = 0;
  for (const auto& [price, level] : levels) {
    if (count == out.size()) {
      break;
    }
    out[count++] = LevelSnapshot{price, level->total_shares(), level->order_count()};
  }
  return count;
}

}  // namespace

LimitOrderBook::LimitOrderBook(u16 stock_locate, BookCapacity capacity)
    : stock_locate_(stock_locate),
      order_pool_(capacity.initial_orders),
      level_pool_(capacity.initial_levels) {
  orders_.reserve(capacity.initial_orders);
}

ApplyResult LimitOrderBook::apply(const BookMessage& message) {
  return std::visit(
      [this](const auto& typed) -> ApplyResult {
        return apply(typed);
      },
      message);
}

ApplyResult LimitOrderBook::apply(const AddOrder& message) {
  return add_order(AddFields{message.order_reference_number,
                             message.timestamp,
                             message.shares,
                             message.price,
                             message.stock_locate,
                             message.buy_sell_indicator,
                             message.type});
}

ApplyResult LimitOrderBook::apply(const AddOrderWithMPID& message) {
  return add_order(AddFields{message.order_reference_number,
                             message.timestamp,
                             message.shares,
                             message.price,
                             message.stock_locate,
                             message.buy_sell_indicator,
                             message.type});
}

ApplyResult LimitOrderBook::apply(const OrderExecuted& message) {
  return execute_order(message.order_reference_number,
                       message.executed_shares,
                       0,
                       false,
                       message.type);
}

ApplyResult LimitOrderBook::apply(const OrderExecutedWithPrice& message) {
  return execute_order(message.order_reference_number,
                       message.executed_shares,
                       message.execution_price,
                       true,
                       message.type);
}

ApplyResult LimitOrderBook::apply(const OrderCancel& message) {
  return cancel_order(message.order_reference_number,
                      message.cancelled_shares,
                      message.type);
}

ApplyResult LimitOrderBook::apply(const OrderDelete& message) {
  return delete_order(message.order_reference_number, message.type);
}

ApplyResult LimitOrderBook::apply(const OrderReplace& message) {
  ApplyResult check = check_stock_locate(
      message.stock_locate, message.type, message.original_order_reference_number);
  if (!check.ok()) {
    return check;
  }

  auto it = orders_.find(message.original_order_reference_number);
  if (it == orders_.end()) {
    return ApplyResult{ApplyStatus::UnknownOrder,
                       message.type,
                       message.original_order_reference_number};
  }
  if (orders_.find(message.new_order_reference_number) != orders_.end()) {
    return ApplyResult{ApplyStatus::DuplicateOrder,
                       message.type,
                       message.new_order_reference_number};
  }
  if (message.shares == 0) {
    return ApplyResult{ApplyStatus::InvalidQuantity,
                       message.type,
                       message.original_order_reference_number};
  }

  const BookOrder* original = it->second;
  const char side = original->side == BookSide::Buy ? 'B' : 'S';
  const AddFields replacement{message.new_order_reference_number,
                              message.timestamp,
                              message.shares,
                              message.price,
                              message.stock_locate,
                              side,
                              message.type};

  const ApplyResult removed =
      remove_order(it->second, message.type);
  if (!removed.ok()) {
    return removed;
  }
  return add_order(replacement);
}

u32 LimitOrderBook::best_bid() const {
  return bids_.empty() ? 0 : bids_.begin()->first;
}

u32 LimitOrderBook::best_ask() const {
  return asks_.empty() ? 0 : asks_.begin()->first;
}

u64 LimitOrderBook::bid_shares_at(u32 price) const {
  const Level* level = bid_level(price);
  return level == nullptr ? 0 : level->total_shares();
}

u64 LimitOrderBook::ask_shares_at(u32 price) const {
  const Level* level = ask_level(price);
  return level == nullptr ? 0 : level->total_shares();
}

u32 LimitOrderBook::bid_order_count_at(u32 price) const {
  const Level* level = bid_level(price);
  return level == nullptr ? 0 : level->order_count();
}

u32 LimitOrderBook::ask_order_count_at(u32 price) const {
  const Level* level = ask_level(price);
  return level == nullptr ? 0 : level->order_count();
}

const Level* LimitOrderBook::bid_level(u32 price) const {
  return find_level(BookSide::Buy, price);
}

const Level* LimitOrderBook::ask_level(u32 price) const {
  return find_level(BookSide::Sell, price);
}

const BookOrder* LimitOrderBook::find_order(u64 reference_number) const {
  const auto it = orders_.find(reference_number);
  return it == orders_.end() ? nullptr : it->second;
}

BookStats LimitOrderBook::stats() const {
  return BookStats{orders_.size(),
                   bids_.size(),
                   asks_.size(),
                   bid_shares_,
                   ask_shares_,
                   visible_executed_shares_,
                   last_executed_price_,
                   last_executed_shares_,
                   order_pool_.capacity(),
                   order_pool_.high_watermark(),
                   order_pool_.expansion_count(),
                   level_pool_.capacity(),
                   level_pool_.high_watermark(),
                   level_pool_.expansion_count()};
}

void LimitOrderBook::log_watermark(std::ostream& out, const char* label) const {
  const BookStats current = stats();
  if (label != nullptr && label[0] != '\0') {
    out << label << ' ';
  }
  out << "live_orders=" << current.orders
      << " bid_levels=" << current.bid_levels
      << " ask_levels=" << current.ask_levels
      << " order_pool_capacity=" << current.order_pool_capacity
      << " order_pool_high_watermark=" << current.order_pool_high_watermark
      << " order_pool_expansions=" << current.order_pool_expansions
      << " level_pool_capacity=" << current.level_pool_capacity
      << " level_pool_high_watermark=" << current.level_pool_high_watermark
      << " level_pool_expansions=" << current.level_pool_expansions;
}

std::size_t LimitOrderBook::fill_bid_levels(std::span<LevelSnapshot> out) const {
  return fill_levels(bids_, out);
}

std::size_t LimitOrderBook::fill_ask_levels(std::span<LevelSnapshot> out) const {
  return fill_levels(asks_, out);
}

ApplyResult LimitOrderBook::add_order(const AddFields& fields) {
  ApplyResult check =
      check_stock_locate(fields.stock_locate, fields.type, fields.reference_number);
  if (!check.ok()) {
    return check;
  }

  const std::optional<BookSide> parsed_side = parse_side(fields.side);
  if (!parsed_side.has_value()) {
    return ApplyResult{ApplyStatus::InvalidSide,
                       fields.type,
                       fields.reference_number};
  }
  const BookSide side = *parsed_side;
  if (fields.shares == 0) {
    return ApplyResult{ApplyStatus::InvalidQuantity,
                       fields.type,
                       fields.reference_number};
  }
  if (orders_.find(fields.reference_number) != orders_.end()) {
    return ApplyResult{ApplyStatus::DuplicateOrder,
                       fields.type,
                       fields.reference_number};
  }

  BookOrder* order = order_pool_.create();
  order->reference_number = fields.reference_number;
  order->timestamp = fields.timestamp;
  order->shares = fields.shares;
  order->price = fields.price;
  order->side = side;
  order->level = nullptr;
  order->prev = nullptr;
  order->next = nullptr;

  Level* level = get_or_create_level(side, fields.price);
  level->push_back(order);
  orders_.emplace(order->reference_number, order);

  if (side == BookSide::Buy) {
    bid_shares_ += order->shares;
  } else {
    ask_shares_ += order->shares;
  }

  return ApplyResult{ApplyStatus::Applied, fields.type, fields.reference_number};
}

ApplyResult LimitOrderBook::execute_order(u64 reference_number,
                                           u32 shares,
                                           u32 execution_price,
                                           bool has_execution_price,
                                           char message_type) {
  if (shares == 0) {
    return ApplyResult{ApplyStatus::InvalidQuantity,
                       message_type,
                       reference_number};
  }

  auto it = orders_.find(reference_number);
  if (it == orders_.end()) {
    return ApplyResult{ApplyStatus::UnknownOrder, message_type, reference_number};
  }
  if (shares > it->second->shares) {
    return ApplyResult{ApplyStatus::InvalidQuantity,
                       message_type,
                       reference_number};
  }

  BookOrder* order = it->second;
  last_executed_price_ = has_execution_price ? execution_price : order->price;
  last_executed_shares_ = shares;
  visible_executed_shares_ += shares;

  if (shares == order->shares) {
    return remove_order(order, message_type);
  }
  return reduce_order(order, shares, message_type);
}

ApplyResult LimitOrderBook::cancel_order(u64 reference_number,
                                          u32 shares,
                                          char message_type) {
  if (shares == 0) {
    return ApplyResult{ApplyStatus::InvalidQuantity,
                       message_type,
                       reference_number};
  }

  auto it = orders_.find(reference_number);
  if (it == orders_.end()) {
    return ApplyResult{ApplyStatus::UnknownOrder, message_type, reference_number};
  }
  if (shares > it->second->shares) {
    return ApplyResult{ApplyStatus::InvalidQuantity,
                       message_type,
                       reference_number};
  }

  return reduce_order(it->second, shares, message_type);
}

ApplyResult LimitOrderBook::delete_order(u64 reference_number, char message_type) {
  auto it = orders_.find(reference_number);
  if (it == orders_.end()) {
    return ApplyResult{ApplyStatus::UnknownOrder, message_type, reference_number};
  }
  return remove_order(it->second, message_type);
}

ApplyResult LimitOrderBook::reduce_order(BookOrder* order,
                                          u32 shares,
                                          char message_type) {
  Level* level = order->level;
  if (level == nullptr) {
    return ApplyResult{ApplyStatus::UnknownOrder,
                       message_type,
                       order->reference_number};
  }
  level->reduce(order, shares);
  if (order->side == BookSide::Buy) {
    bid_shares_ -= shares;
  } else {
    ask_shares_ -= shares;
  }

  if (order->shares == 0) {
    return remove_order(order, message_type);
  }

  return ApplyResult{ApplyStatus::Applied, message_type, order->reference_number};
}

ApplyResult LimitOrderBook::remove_order(BookOrder* order, char message_type) {
  Level* level = order->level;
  if (level == nullptr) {
    return ApplyResult{
        ApplyStatus::UnknownOrder, message_type, order->reference_number};
  }
  const u32 remaining_shares = order->shares;
  level->remove(order);
  orders_.erase(order->reference_number);

  if (order->side == BookSide::Buy) {
    bid_shares_ -= remaining_shares;
  } else {
    ask_shares_ -= remaining_shares;
  }

  const u64 reference_number = order->reference_number;
  erase_level_if_empty(order->side, level);
  order_pool_.destroy(order);
  return ApplyResult{ApplyStatus::Applied, message_type, reference_number};
}

ApplyResult LimitOrderBook::check_stock_locate(u16 stock_locate,
                                                char message_type,
                                                u64 ref) const {
  if (stock_locate_ != 0 && stock_locate != 0 && stock_locate != stock_locate_) {
    return ApplyResult{ApplyStatus::StockLocateMismatch, message_type, ref};
  }
  return ApplyResult{ApplyStatus::Applied, message_type, ref};
}

Level* LimitOrderBook::get_or_create_level(BookSide side, u32 price) {
  if (side == BookSide::Buy) {
    auto it = bids_.find(price);
    if (it != bids_.end()) {
      return it->second;
    }
    Level* level = level_pool_.create(price);
    bids_.emplace(price, level);
    return level;
  }

  auto it = asks_.find(price);
  if (it != asks_.end()) {
    return it->second;
  }
  Level* level = level_pool_.create(price);
  asks_.emplace(price, level);
  return level;
}

void LimitOrderBook::erase_level_if_empty(BookSide side, Level* level) {
  if (level == nullptr || !level->empty()) {
    return;
  }

  const u32 price = level->price();
  if (side == BookSide::Buy) {
    bids_.erase(price);
  } else {
    asks_.erase(price);
  }
  level_pool_.destroy(level);
}

const Level* LimitOrderBook::find_level(BookSide side, u32 price) const {
  if (side == BookSide::Buy) {
    const auto it = bids_.find(price);
    return it == bids_.end() ? nullptr : it->second;
  }
  const auto it = asks_.find(price);
  return it == asks_.end() ? nullptr : it->second;
}

std::optional<BookSide> LimitOrderBook::parse_side(char side) {
  if (side == 'B') {
    return BookSide::Buy;
  }
  if (side == 'S') {
    return BookSide::Sell;
  }
  return std::nullopt;
}

}  // namespace itch
