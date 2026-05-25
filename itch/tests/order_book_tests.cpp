#include "book_snapshot.hpp"
#include "message_accessors.hpp"
#include "order_book.hpp"

#include <cstdlib>
#include <new>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++failures;
  }
}

void expect_status(const itch::ApplyResult& result,
                   itch::ApplyStatus status,
                   std::string_view message) {
  expect(result.status == status, message);
}

itch::AddOrder add(itch::u64 ref,
                   char side,
                   itch::u32 shares,
                   itch::u32 price,
                   itch::u16 locate = 13,
                   itch::u64 timestamp = 100) {
  itch::AddOrder message;
  message.type = 'A';
  message.stock_locate = locate;
  message.timestamp = timestamp;
  message.order_reference_number = ref;
  message.buy_sell_indicator = side;
  message.shares = shares;
  message.price = price;
  return message;
}

itch::OrderExecuted executed(itch::u64 ref, itch::u32 shares) {
  itch::OrderExecuted message;
  message.type = 'E';
  message.stock_locate = 13;
  message.order_reference_number = ref;
  message.executed_shares = shares;
  return message;
}

itch::OrderExecutedWithPrice executed_with_price(itch::u64 ref,
                                                  itch::u32 shares,
                                                  itch::u32 price) {
  itch::OrderExecutedWithPrice message;
  message.type = 'C';
  message.stock_locate = 13;
  message.order_reference_number = ref;
  message.executed_shares = shares;
  message.execution_price = price;
  message.printable = 'Y';
  return message;
}

itch::OrderCancel cancel(itch::u64 ref, itch::u32 shares) {
  itch::OrderCancel message;
  message.type = 'X';
  message.stock_locate = 13;
  message.order_reference_number = ref;
  message.cancelled_shares = shares;
  return message;
}

itch::OrderDelete erase(itch::u64 ref) {
  itch::OrderDelete message;
  message.type = 'D';
  message.stock_locate = 13;
  message.order_reference_number = ref;
  return message;
}

itch::OrderReplace replace(itch::u64 old_ref,
                           itch::u64 new_ref,
                           itch::u32 shares,
                           itch::u32 price) {
  itch::OrderReplace message;
  message.type = 'U';
  message.stock_locate = 13;
  message.original_order_reference_number = old_ref;
  message.new_order_reference_number = new_ref;
  message.shares = shares;
  message.price = price;
  return message;
}

void test_add_best_and_same_price_both_sides() {
  itch::LimitOrderBook book(13, itch::BookCapacity{64, 16});

  expect_status(book.apply(add(1, 'B', 100, 1000)),
                itch::ApplyStatus::Applied,
                "add first bid");
  expect_status(book.apply(add(2, 'B', 50, 1010)),
                itch::ApplyStatus::Applied,
                "add better bid");
  expect_status(book.apply(add(3, 'S', 70, 1000)),
                itch::ApplyStatus::Applied,
                "add ask at same price as bid");
  expect_status(book.apply(add(4, 'S', 80, 990)),
                itch::ApplyStatus::Applied,
                "add better ask");

  expect(book.best_bid() == 1010, "best bid is highest bid price");
  expect(book.best_ask() == 990, "best ask is lowest ask price");
  expect(book.bid_shares_at(1000) == 100, "bid shares at 1000");
  expect(book.ask_shares_at(1000) == 70, "ask shares at same 1000 price");
  expect(book.stats().orders == 4, "four live orders");
  expect(book.stats().bid_levels == 2, "two bid levels");
  expect(book.stats().ask_levels == 2, "two ask levels");
}

void test_fifo_reduce_and_delete() {
  itch::LimitOrderBook book(13, itch::BookCapacity{64, 16});
  book.apply(add(10, 'B', 100, 1000, 13, 1));
  book.apply(add(11, 'B', 200, 1000, 13, 2));

  const itch::Level* level = book.bid_level(1000);
  expect(level != nullptr, "level exists after adds");
  expect(level->front() != nullptr && level->front()->reference_number == 10,
         "FIFO front is first order");
  expect(level->back() != nullptr && level->back()->reference_number == 11,
         "FIFO back is second order");
  expect(book.find_order(10) != nullptr && book.find_order(10)->level == level,
         "order points back to its level");

  expect_status(book.apply(executed(10, 40)),
                itch::ApplyStatus::Applied,
                "partial execute first order");
  expect(book.find_order(10) != nullptr && book.find_order(10)->shares == 60,
         "partial execution reduces first order");
  expect(book.bid_shares_at(1000) == 260, "level volume after partial execution");
  expect(book.stats().visible_executed_shares == 40, "partial execution increments volume");

  expect_status(book.apply(executed(10, 60)),
                itch::ApplyStatus::Applied,
                "complete execute first order");
  level = book.bid_level(1000);
  expect(level != nullptr && level->front() != nullptr &&
             level->front()->reference_number == 11,
         "FIFO advances after first order filled");
  expect(book.find_order(10) == nullptr, "filled order removed");
  expect(book.bid_shares_at(1000) == 200, "level volume after fill");
  expect(book.stats().visible_executed_shares == 100, "complete execution increments volume");

  expect_status(book.apply(erase(11)),
                itch::ApplyStatus::Applied,
                "delete remaining order");
  expect(book.bid_level(1000) == nullptr, "empty level removed");
  expect(book.best_bid() == 0, "best bid empty sentinel");
}

void test_cancel_replace_and_execution_stats() {
  itch::LimitOrderBook book(13, itch::BookCapacity{64, 16});
  book.apply(add(20, 'S', 100, 1050));

  expect_status(book.apply(cancel(20, 30)),
                itch::ApplyStatus::Applied,
                "cancel ask");
  expect(book.find_order(20) != nullptr && book.find_order(20)->shares == 70,
         "cancel reduces order");
  expect(book.ask_shares_at(1050) == 70, "cancel reduces level");

  expect_status(book.apply(replace(20, 21, 40, 1040)),
                itch::ApplyStatus::Applied,
                "replace ask");
  expect(book.find_order(20) == nullptr, "old ref removed by replace");
  expect(book.find_order(21) != nullptr, "new ref inserted by replace");
  expect(book.ask_level(1050) == nullptr, "old replace level removed");
  expect(book.best_ask() == 1040, "replace updates best ask");
  expect(book.find_order(21)->level == book.ask_level(1040),
         "replaced order points to new level");

  expect_status(book.apply(executed_with_price(21, 10, 1035)),
                itch::ApplyStatus::Applied,
                "execute with price");
  expect(book.stats().last_executed_price == 1035, "last execution price tracked");
  expect(book.stats().last_executed_shares == 10, "last execution shares tracked");
  expect(book.stats().visible_executed_shares == 10, "with-price execution increments volume");
  expect(book.ask_shares_at(1040) == 30, "execution reduces ask level");

  expect_status(book.apply(cancel(21, 30)),
                itch::ApplyStatus::Applied,
                "full cancel removes order");
  expect(book.find_order(21) == nullptr, "full cancel removes order ref");
  expect(book.ask_level(1040) == nullptr, "full cancel removes level");
  expect(book.stats().orders == 0, "full cancel leaves book empty");
}

void test_book_message_variant_and_errors() {
  itch::LimitOrderBook book(13, itch::BookCapacity{64, 16});
  itch::Message feed_message = add(30, 'B', 10, 1000);
  std::optional<itch::BookMessage> message = itch::to_book_message(feed_message);

  expect(message.has_value(), "feed add converts to book message");
  if (message.has_value()) {
    expect_status(book.apply(*message),
                  itch::ApplyStatus::Applied,
                  "book variant apply routes add");
    expect_status(book.apply(*message),
                  itch::ApplyStatus::DuplicateOrder,
                  "duplicate order detected");
  }
  expect_status(book.apply(add(31, 'Z', 10, 1000)),
                itch::ApplyStatus::InvalidSide,
                "invalid side detected");
  expect_status(book.apply(add(32, 'B', 10, 1000, 99)),
                itch::ApplyStatus::StockLocateMismatch,
                "stock locate mismatch detected");
  expect_status(book.apply(cancel(999, 1)),
                itch::ApplyStatus::UnknownOrder,
                "unknown order detected");

  itch::SystemEvent event;
  event.type = 'S';
  feed_message = event;
  expect(!itch::to_book_message(feed_message).has_value(),
         "non-book message is not routed to order book");
}

void test_pool_expansion_and_watermarks() {
  itch::LimitOrderBook book(13, itch::BookCapacity{2, 1});

  expect_status(book.apply(add(40, 'B', 10, 1000)),
                itch::ApplyStatus::Applied,
                "first order fits initial pool");
  expect_status(book.apply(add(41, 'B', 10, 1001)),
                itch::ApplyStatus::Applied,
                "second order fits initial pool");

  itch::BookStats stats = book.stats();
  expect(stats.order_pool_capacity == 2, "initial order pool capacity");
  expect(stats.order_pool_high_watermark == 2, "initial order pool watermark");
  expect(stats.order_pool_expansions == 0, "no order pool expansion yet");
  expect(stats.level_pool_capacity == 2, "level pool expanded for second price");
  expect(stats.level_pool_high_watermark == 2, "level pool watermark after second price");
  expect(stats.level_pool_expansions == 1, "level pool expansion counted");

  expect_status(book.apply(add(42, 'B', 10, 1002)),
                itch::ApplyStatus::Applied,
                "third order silently expands order pool");
  stats = book.stats();
  expect(stats.orders == 3, "three live orders after expansion");
  expect(stats.order_pool_capacity == 4, "order pool geometric expansion");
  expect(stats.order_pool_high_watermark == 3, "order pool high watermark after expansion");
  expect(stats.order_pool_expansions == 1, "order pool expansion counted");

  expect_status(book.apply(erase(42)),
                itch::ApplyStatus::Applied,
                "delete after expansion");
  stats = book.stats();
  expect(stats.orders == 2, "live orders after delete");
  expect(stats.order_pool_high_watermark == 3, "watermark survives delete");
}

void test_book_snapshot_buffer() {
  itch::LimitOrderBook book(13, itch::BookCapacity{64, 16});
  itch::BookSnapshotBuffer snapshot(2);

  expect_status(book.apply(add(50, 'B', 100, 1000)),
                itch::ApplyStatus::Applied,
                "snapshot bid add");
  expect_status(book.apply(add(51, 'B', 200, 990)),
                itch::ApplyStatus::Applied,
                "snapshot second bid add");
  expect_status(book.apply(add(52, 'S', 150, 1010)),
                itch::ApplyStatus::Applied,
                "snapshot ask add");
  expect_status(book.apply(add(53, 'S', 250, 1020)),
                itch::ApplyStatus::Applied,
                "snapshot second ask add");

  const itch::L2SnapshotView view = snapshot.capture(book);
  expect(view.best_bid == 1000, "snapshot best bid");
  expect(view.best_ask == 1010, "snapshot best ask");
  expect(view.bid_level_count == 2, "snapshot bid depth");
  expect(view.ask_level_count == 2, "snapshot ask depth");
  expect(view.bid_levels != nullptr && view.bid_levels[0].price == 1000 &&
             view.bid_levels[0].shares == 100,
         "snapshot first bid level");
  expect(view.ask_levels != nullptr && view.ask_levels[1].price == 1020 &&
             view.ask_levels[1].shares == 250,
         "snapshot second ask level");
}

}  // namespace

int main() {
  test_add_best_and_same_price_both_sides();
  test_fifo_reduce_and_delete();
  test_cancel_replace_and_execution_stats();
  test_book_message_variant_and_errors();
  test_pool_expansion_and_watermarks();
  test_book_snapshot_buffer();

  if (failures != 0) {
    std::cerr << failures << " order book test failure(s)\n";
    return EXIT_FAILURE;
  }

  std::cout << "order_book_tests passed\n";
  return EXIT_SUCCESS;
}
