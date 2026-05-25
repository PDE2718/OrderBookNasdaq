#include "book_snapshot.hpp"
#include "fixed_ascii.hpp"
#include "frame_reader.hpp"
#include "l2_snapshot_collector.hpp"
#include "message_accessors.hpp"
#include "messages.hpp"
#include "parser.hpp"
#include "snapshot_h5_writer.hpp"
#include "types.hpp"
#include "unordered_dense.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using itch::u16;
using itch::u32;
using itch::u64;
using itch::BarL2SnapshotCollector;
using itch::BookSnapshotBuffer;
using itch::BookOrder;
using itch::BookCapacity;
using itch::FrameReader;
using itch::LimitOrderBook;
using itch::Message;
using itch::MessageBodyView;
using itch::Stock;
using itch::tools::SnapshotBatch;
using itch::tools::SnapshotH5Writer;

namespace {

constexpr u64 kNsPerMillisecond = 1'000'000ULL;
constexpr u64 kNsPerSecond = 1'000'000'000ULL;
constexpr u64 kSessionStartNs = (9ULL * 3600 + 30ULL * 60) * kNsPerSecond;
constexpr u64 kSessionEndNs = 16ULL * 3600 * kNsPerSecond;
constexpr u64 kSessionNs = kSessionEndNs - kSessionStartNs;

std::size_t choose_batch_size(std::size_t bar_count) {
  return std::min<std::size_t>(4096, std::max<std::size_t>(1, bar_count));
}

std::string stock_to_string(Stock stock) {
  return std::string(stock.trimmed());
}

std::string uppercase(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::vector<std::string> split_symbols(const std::string& text) {
  std::vector<std::string> symbols;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item.erase(std::remove_if(item.begin(), item.end(),
                              [](unsigned char ch) { return std::isspace(ch); }),
               item.end());
    if (!item.empty()) {
      symbols.push_back(uppercase(item));
    }
  }
  return symbols;
}

u64 parse_positive_integer(const std::string& text, const std::string& field_name) {
  std::size_t consumed = 0;
  const unsigned long long value = std::stoull(text, &consumed);
  if (consumed != text.size() || value == 0) {
    throw std::runtime_error("invalid " + field_name + ": " + text);
  }
  return static_cast<u64>(value);
}

u32 parse_u32_arg(const std::string& text, const std::string& field_name) {
  const u64 value = parse_positive_integer(text, field_name);
  if (value > std::numeric_limits<u32>::max()) {
    throw std::runtime_error(field_name + " is too large: " + text);
  }
  return static_cast<u32>(value);
}

u64 parse_interval_ns(const std::string& text) {
  const std::array<std::pair<const char*, u64>, 5> units{{
      {"ms", kNsPerMillisecond},
      {"ns", 1ULL},
      {"us", 1'000ULL},
      {"s", kNsPerSecond},
      {"m", 60ULL * kNsPerSecond},
  }};

  for (const auto& [suffix, multiplier] : units) {
    const std::string suffix_text(suffix);
    if (text.size() <= suffix_text.size()) {
      continue;
    }
    if (text.compare(text.size() - suffix_text.size(), suffix_text.size(), suffix_text) != 0) {
      continue;
    }

    const std::string number = text.substr(0, text.size() - suffix_text.size());
    const u64 value = parse_positive_integer(number, "interval");
    if (value > std::numeric_limits<u64>::max() / multiplier) {
      throw std::runtime_error("interval is too large: " + text);
    }
    return value * multiplier;
  }

  throw std::runtime_error("interval must use one of: ns, us, ms, s, m");
}

struct VisibleTrade {
  u32 price = 0;
  u32 shares = 0;
};

VisibleTrade visible_trade_of(const Message& message, const LimitOrderBook& book) {
  if (const auto* executed = std::get_if<itch::OrderExecuted>(&message)) {
    const BookOrder* order = book.find_order(executed->order_reference_number);
    if (order == nullptr) {
      return {};
    }
    return {order->price, executed->executed_shares};
  }

  if (const auto* executed = std::get_if<itch::OrderExecutedWithPrice>(&message)) {
    if (executed->printable != 'Y') {
      return {};
    }
    return {executed->execution_price, executed->executed_shares};
  }

  return {};
}

struct SymbolContext {
  std::string symbol;
  u16 stock_locate = 0;
  LimitOrderBook book;
  BookSnapshotBuffer snapshot;
  BarL2SnapshotCollector collector;
  SnapshotBatch batch;
  SnapshotH5Writer writer;
  std::size_t next_bar = 0;
  u64 messages = 0;
  u64 book_messages = 0;
  u64 errors = 0;

  SymbolContext(std::string symbol_value,
                const std::filesystem::path& output_root,
                u64 interval_ns,
                std::size_t depth,
                std::size_t bar_count,
                std::size_t batch_size,
                BookCapacity book_capacity)
      : symbol(std::move(symbol_value)),
        book(0, book_capacity),
        snapshot(depth),
        collector(kSessionStartNs, kSessionEndNs, interval_ns),
        batch(depth, batch_size),
        writer(output_root,
               symbol,
               kSessionStartNs,
               kSessionEndNs,
               interval_ns,
               depth,
               bar_count,
               batch_size) {}
};

void flush_batch(SymbolContext& ctx) {
  if (ctx.batch.empty()) {
    return;
  }
  ctx.writer.write_batch(ctx.batch);
  ctx.batch.clear();
}

void emit_next_bar(SymbolContext& ctx) {
  const u64 start_ns = ctx.collector.session_start_ns() +
                       ctx.collector.interval_ns() * ctx.next_bar;
  const u64 end_ns = start_ns + ctx.collector.interval_ns();
  ctx.batch.push(ctx.collector.capture(start_ns, end_ns, ctx.snapshot.capture(ctx.book)));
  if (ctx.batch.full()) {
    flush_batch(ctx);
  }
  ctx.collector.reset_bar();
  ++ctx.next_bar;
}

void advance_to_bar(SymbolContext& ctx, std::size_t target_bar) {
  while (ctx.next_bar < target_bar) {
    emit_next_bar(ctx);
  }
}

void print_usage() {
  std::cerr
      << "Usage: export_l2_snapshots --input FILE --symbols INTC,AAPL "
      << "--output DIR [--interval 30s] [--depth 10] "
      << "[--initial-orders N] [--initial-levels N]\n"
      << "Writes one HDF5 file per symbol at DIR/SYMBOL/snapshot.h5.\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string input;
    std::string symbols_text;
    std::filesystem::path output_root = "l2_hdf5_snapshots";
    std::string interval_text = "30s";
    std::size_t depth = 10;
    BookCapacity book_capacity;

    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      auto require_value = [&](const std::string& name) -> std::string {
        if (i + 1 >= argc) {
          throw std::runtime_error("missing value for " + name);
        }
        return argv[++i];
      };

      if (arg == "--input") {
        input = require_value(arg);
      } else if (arg == "--symbols") {
        symbols_text = require_value(arg);
      } else if (arg == "--output") {
        output_root = require_value(arg);
      } else if (arg == "--interval") {
        interval_text = require_value(arg);
      } else if (arg == "--depth") {
        depth = static_cast<std::size_t>(parse_positive_integer(require_value(arg), "depth"));
      } else if (arg == "--initial-orders") {
        book_capacity.initial_orders = parse_u32_arg(require_value(arg), "initial-orders");
      } else if (arg == "--initial-levels") {
        book_capacity.initial_levels = parse_u32_arg(require_value(arg), "initial-levels");
      } else if (arg == "--help" || arg == "-h") {
        print_usage();
        return EXIT_SUCCESS;
      } else {
        throw std::runtime_error("unknown argument: " + arg);
      }
    }

    if (input.empty() || symbols_text.empty()) {
      print_usage();
      return EXIT_FAILURE;
    }
    if (depth == 0) {
      throw std::runtime_error("depth must be positive");
    }

    const u64 interval_ns = parse_interval_ns(interval_text);
    if (interval_ns < kNsPerMillisecond) {
      throw std::runtime_error("minimum supported interval is 1ms");
    }
    if (kSessionNs % interval_ns != 0) {
      throw std::runtime_error("interval_ns must divide regular session length exactly");
    }

    const std::size_t bar_count = static_cast<std::size_t>(kSessionNs / interval_ns);
    const std::size_t batch_size = choose_batch_size(bar_count);
    const std::vector<std::string> symbols = split_symbols(symbols_text);
    if (symbols.empty()) {
      throw std::runtime_error("no symbols requested");
    }

    std::filesystem::create_directories(output_root);
    ankerl::unordered_dense::map<std::string, std::size_t> symbol_index;
    ankerl::unordered_dense::map<u16, std::size_t> locate_index;
    std::vector<std::unique_ptr<SymbolContext>> contexts;
    contexts.reserve(symbols.size());

    for (const std::string& symbol : symbols) {
      if (symbol_index.find(symbol) != symbol_index.end()) {
        throw std::runtime_error("duplicate symbol: " + symbol);
      }
      const std::size_t index = contexts.size();
      symbol_index.emplace(symbol, index);
      contexts.push_back(std::make_unique<SymbolContext>(
          symbol,
          output_root,
          interval_ns,
          depth,
          bar_count,
          batch_size,
          book_capacity));
    }

    FrameReader reader(input);
    MessageBodyView view;
    u64 file_messages = 0;

    while (reader.next(view)) {
      ++file_messages;
      const Message message = itch::parse_message_body(view);

      if (const auto* directory = std::get_if<itch::StockDirectory>(&message)) {
        const auto it = symbol_index.find(stock_to_string(directory->stock));
        if (it != symbol_index.end()) {
          SymbolContext& ctx = *contexts[it->second];
          ctx.stock_locate = directory->stock_locate;
          ctx.book.set_stock_locate(directory->stock_locate);
          locate_index.emplace(directory->stock_locate, it->second);
          std::cout << "Resolved " << ctx.symbol << " locate=" << ctx.stock_locate << "\n";
        }
        continue;
      }

      const auto locate_it = locate_index.find(itch::message_stock_locate(message));
      if (locate_it == locate_index.end()) {
        continue;
      }

      SymbolContext& ctx = *contexts[locate_it->second];
      ++ctx.messages;
      const u64 timestamp_ns = itch::message_timestamp(message);
      const bool in_regular_session = ctx.collector.in_session(timestamp_ns);

      if (in_regular_session) {
        advance_to_bar(ctx, ctx.collector.bars_ended_before(timestamp_ns));
        const VisibleTrade trade = visible_trade_of(message, ctx.book);
        ctx.collector.add_trade(trade.price, trade.shares);
      } else if (timestamp_ns > kSessionEndNs) {
        advance_to_bar(ctx, bar_count);
        continue;
      }

      const std::optional<itch::BookMessage> book_message =
          itch::to_book_message(message);
      if (!book_message.has_value()) {
        if (in_regular_session) {
          advance_to_bar(ctx, ctx.collector.bars_ended_at_or_before(timestamp_ns));
        }
        continue;
      }

      ++ctx.book_messages;
      const itch::ApplyResult result = ctx.book.apply(*book_message);
      if (!result.ok()) {
        ++ctx.errors;
      }

      if (in_regular_session) {
        advance_to_bar(ctx, ctx.collector.bars_ended_at_or_before(timestamp_ns));
      }
    }

    for (auto& ctx : contexts) {
      advance_to_bar(*ctx, bar_count);
      flush_batch(*ctx);
      ctx->writer.close();
      std::cout << "Wrote " << ctx->symbol
                << " path=" << ctx->writer.output_path().string()
                << " bars=" << bar_count
                << " messages=" << ctx->messages
                << " book_messages=" << ctx->book_messages
                << " errors=" << ctx->errors
                << " ";
      ctx->book.log_watermark(std::cout);
      std::cout << "\n";
    }

    std::cout << "Export complete: input=" << input
              << " output=" << output_root.string()
              << " file_messages=" << file_messages
              << " interval_ns=" << interval_ns
              << " depth=" << depth
              << " bars=" << bar_count << "\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
}
