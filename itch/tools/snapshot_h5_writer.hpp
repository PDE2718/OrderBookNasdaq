#pragma once

#include "l2_snapshot.hpp"
#include "types.hpp"

#include <highfive/highfive.hpp>

#include <algorithm>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace itch::tools {

class SnapshotBatch {
 public:
  SnapshotBatch(std::size_t depth, std::size_t capacity)
      : depth_(depth), capacity_(capacity) {
    if (depth_ == 0) {
      throw std::invalid_argument("SnapshotBatch depth must be positive");
    }
    if (capacity_ == 0) {
      throw std::invalid_argument("SnapshotBatch capacity must be positive");
    }
    resize_columns();
  }

  void push(const BarSnapshotView& snapshot) {
    if (full()) {
      throw std::runtime_error("SnapshotBatch is full");
    }

    const std::size_t row = size_;
    bar_.start_ns[row] = snapshot.interval_start_ns;
    bar_.end_ns[row] = snapshot.interval_end_ns;
    bar_.open_px[row] = snapshot.open;
    bar_.high_px[row] = snapshot.high;
    bar_.low_px[row] = snapshot.low;
    bar_.close_px[row] = snapshot.close;
    bar_.volume[row] = snapshot.volume;
    bar_.notional[row] = snapshot.notional;
    bar_.has_trade[row] = snapshot.has_trade ? 1 : 0;
    tob_.best_bid[row] = snapshot.book.best_bid;
    tob_.best_ask[row] = snapshot.book.best_ask;

    copy_levels(row,
                {snapshot.book.bid_levels, snapshot.book.bid_level_count},
                book_.bid_px,
                book_.bid_shares,
                book_.bid_orders);
    copy_levels(row,
                {snapshot.book.ask_levels, snapshot.book.ask_level_count},
                book_.ask_px,
                book_.ask_shares,
                book_.ask_orders);

    ++size_;
  }

  void clear() noexcept { size_ = 0; }

  bool empty() const noexcept { return size_ == 0; }
  bool full() const noexcept { return size_ == capacity_; }
  std::size_t size() const noexcept { return size_; }
  std::size_t depth() const noexcept { return depth_; }

 private:
  struct BarColumns {
    std::vector<u64> start_ns;
    std::vector<u64> end_ns;
    std::vector<u32> open_px;
    std::vector<u32> high_px;
    std::vector<u32> low_px;
    std::vector<u32> close_px;
    std::vector<u64> volume;
    std::vector<u64> notional;
    std::vector<u8> has_trade;
  };

  struct TopOfBookColumns {
    std::vector<u32> best_bid;
    std::vector<u32> best_ask;
  };

  struct BookColumns {
    std::vector<u32> bid_px;
    std::vector<u64> bid_shares;
    std::vector<u32> bid_orders;
    std::vector<u32> ask_px;
    std::vector<u64> ask_shares;
    std::vector<u32> ask_orders;
  };

  void resize_columns() {
    bar_.start_ns.resize(capacity_);
    bar_.end_ns.resize(capacity_);
    bar_.open_px.resize(capacity_);
    bar_.high_px.resize(capacity_);
    bar_.low_px.resize(capacity_);
    bar_.close_px.resize(capacity_);
    bar_.volume.resize(capacity_);
    bar_.notional.resize(capacity_);
    bar_.has_trade.resize(capacity_);
    tob_.best_bid.resize(capacity_);
    tob_.best_ask.resize(capacity_);

    const std::size_t cells = capacity_ * depth_;
    book_.bid_px.resize(cells);
    book_.bid_shares.resize(cells);
    book_.bid_orders.resize(cells);
    book_.ask_px.resize(cells);
    book_.ask_shares.resize(cells);
    book_.ask_orders.resize(cells);
  }

  void copy_levels(std::size_t row,
                   std::span<const LevelSnapshot> levels,
                   std::vector<u32>& px,
                   std::vector<u64>& shares,
                   std::vector<u32>& orders) {
    const std::size_t offset = row * depth_;
    const std::size_t count = std::min(depth_, levels.size());
    for (std::size_t i = 0; i < count; ++i) {
      px[offset + i] = levels[i].price;
      shares[offset + i] = levels[i].shares;
      orders[offset + i] = levels[i].orders;
    }
    for (std::size_t i = count; i < depth_; ++i) {
      px[offset + i] = 0;
      shares[offset + i] = 0;
      orders[offset + i] = 0;
    }
  }

  std::size_t depth_ = 0;
  std::size_t capacity_ = 0;
  std::size_t size_ = 0;
  BarColumns bar_;
  BookColumns book_;
  TopOfBookColumns tob_;

  friend class SnapshotH5Writer;
};

class SnapshotH5Writer {
 public:
  SnapshotH5Writer(const std::filesystem::path& output_root,
                   const std::string& symbol,
                   u64 session_start_ns,
                   u64 session_end_ns,
                   u64 interval_ns,
                   std::size_t depth,
                   std::size_t bar_count,
                   std::size_t chunk_rows)
      : output_path_(output_root / symbol / "snapshot.h5"),
        depth_(depth),
        bar_count_(bar_count),
        file_(open_file(output_root, symbol)),
        bar_group_(file_.createGroup("bar")),
        book_group_(file_.createGroup("book")),
        bar_(create_bar_datasets(bar_group_, bar_count_, chunk_rows)),
        book_(create_book_datasets(book_group_, bar_count_, depth_, chunk_rows)) {
    write_metadata(symbol, session_start_ns, session_end_ns, interval_ns);
  }

  const std::filesystem::path& output_path() const noexcept { return output_path_; }

  void write_batch(const SnapshotBatch& batch) {
    if (rows_written_ + batch.size() > bar_count_) {
      throw std::runtime_error("too many bars written to " + output_path_.string());
    }

    write_1d(bar_.start_ns, batch.bar_.start_ns, batch.size());
    write_1d(bar_.end_ns, batch.bar_.end_ns, batch.size());
    write_1d(bar_.open_px, batch.bar_.open_px, batch.size());
    write_1d(bar_.high_px, batch.bar_.high_px, batch.size());
    write_1d(bar_.low_px, batch.bar_.low_px, batch.size());
    write_1d(bar_.close_px, batch.bar_.close_px, batch.size());
    write_1d(bar_.volume, batch.bar_.volume, batch.size());
    write_1d(bar_.notional, batch.bar_.notional, batch.size());
    write_1d(bar_.has_trade, batch.bar_.has_trade, batch.size());
    write_1d(book_.best_bid, batch.tob_.best_bid, batch.size());
    write_1d(book_.best_ask, batch.tob_.best_ask, batch.size());

    write_2d(book_.bid_px, batch.book_.bid_px, batch.size(), batch.depth());
    write_2d(book_.bid_shares, batch.book_.bid_shares, batch.size(), batch.depth());
    write_2d(book_.bid_orders, batch.book_.bid_orders, batch.size(), batch.depth());
    write_2d(book_.ask_px, batch.book_.ask_px, batch.size(), batch.depth());
    write_2d(book_.ask_shares, batch.book_.ask_shares, batch.size(), batch.depth());
    write_2d(book_.ask_orders, batch.book_.ask_orders, batch.size(), batch.depth());

    rows_written_ += batch.size();
  }

  void close() {
    if (rows_written_ != bar_count_) {
      throw std::runtime_error("HDF5 writer closed before all bars were written");
    }
    file_.flush();
  }

 private:
  struct BarDataSets {
    HighFive::DataSet start_ns;
    HighFive::DataSet end_ns;
    HighFive::DataSet open_px;
    HighFive::DataSet high_px;
    HighFive::DataSet low_px;
    HighFive::DataSet close_px;
    HighFive::DataSet volume;
    HighFive::DataSet notional;
    HighFive::DataSet has_trade;
  };

  struct BookDataSets {
    HighFive::DataSet best_bid;
    HighFive::DataSet best_ask;
    HighFive::DataSet bid_px;
    HighFive::DataSet bid_shares;
    HighFive::DataSet bid_orders;
    HighFive::DataSet ask_px;
    HighFive::DataSet ask_shares;
    HighFive::DataSet ask_orders;
  };

  template <typename T>
  static HighFive::DataSet create_dataset(HighFive::Group& group,
                                          std::string_view name,
                                          std::vector<std::size_t> dims,
                                          std::vector<hsize_t> chunk_dims) {
    HighFive::DataSetCreateProps props;
    props.add(HighFive::Chunking(std::move(chunk_dims)));
    return group.createDataSet<T>(std::string(name),
                                  HighFive::DataSpace(std::move(dims)),
                                  props);
  }

  static HighFive::File open_file(const std::filesystem::path& output_root,
                                  const std::string& symbol) {
    const std::filesystem::path symbol_dir = output_root / symbol;
    std::filesystem::create_directories(symbol_dir);
    return HighFive::File((symbol_dir / "snapshot.h5").string(), HighFive::File::Truncate);
  }

  static BarDataSets create_bar_datasets(HighFive::Group& group,
                                         std::size_t bar_count,
                                         std::size_t chunk_rows) {
    return BarDataSets{
        create_dataset<u64>(group, "start_ns", {bar_count}, {chunk_rows}),
        create_dataset<u64>(group, "end_ns", {bar_count}, {chunk_rows}),
        create_dataset<u32>(group, "open_px", {bar_count}, {chunk_rows}),
        create_dataset<u32>(group, "high_px", {bar_count}, {chunk_rows}),
        create_dataset<u32>(group, "low_px", {bar_count}, {chunk_rows}),
        create_dataset<u32>(group, "close_px", {bar_count}, {chunk_rows}),
        create_dataset<u64>(group, "volume", {bar_count}, {chunk_rows}),
        create_dataset<u64>(group, "notional", {bar_count}, {chunk_rows}),
        create_dataset<u8>(group, "has_trade", {bar_count}, {chunk_rows}),
    };
  }

  static BookDataSets create_book_datasets(HighFive::Group& group,
                                           std::size_t bar_count,
                                           std::size_t depth,
                                           std::size_t chunk_rows) {
    return BookDataSets{
        create_dataset<u32>(group, "best_bid", {bar_count}, {chunk_rows}),
        create_dataset<u32>(group, "best_ask", {bar_count}, {chunk_rows}),
        create_dataset<u32>(group, "bid_px", {bar_count, depth}, {chunk_rows, depth}),
        create_dataset<u64>(group, "bid_shares", {bar_count, depth}, {chunk_rows, depth}),
        create_dataset<u32>(group, "bid_orders", {bar_count, depth}, {chunk_rows, depth}),
        create_dataset<u32>(group, "ask_px", {bar_count, depth}, {chunk_rows, depth}),
        create_dataset<u64>(group, "ask_shares", {bar_count, depth}, {chunk_rows, depth}),
        create_dataset<u32>(group, "ask_orders", {bar_count, depth}, {chunk_rows, depth}),
    };
  }

  void write_metadata(const std::string& symbol,
                      u64 session_start_ns,
                      u64 session_end_ns,
                      u64 interval_ns) {
    file_.createAttribute("schema", std::string("itch.l2_snapshot.v1"));
    file_.createAttribute("symbol", symbol);
    file_.createAttribute("volume_policy", std::string("E + printable C"));
    file_.createAttribute(
        "bar_semantics",
        std::string("(start, end] with L2 after processing end timestamp"));
    file_.createAttribute("schema_version", u32{1});
    file_.createAttribute("session_start_ns", session_start_ns);
    file_.createAttribute("session_end_ns", session_end_ns);
    file_.createAttribute("interval_ns", interval_ns);
    file_.createAttribute("bar_count", static_cast<u64>(bar_count_));
    file_.createAttribute("depth", static_cast<u64>(depth_));
    file_.createAttribute("price_scale", u32{10000});
  }

  template <typename T>
  void write_1d(HighFive::DataSet& dataset,
                const std::vector<T>& values,
                std::size_t count) {
    if (count == 0) {
      return;
    }
    dataset.select({rows_written_}, {count}).write_raw(values.data());
  }

  template <typename T>
  void write_2d(HighFive::DataSet& dataset,
                const std::vector<T>& values,
                std::size_t rows,
                std::size_t cols) {
    if (rows == 0) {
      return;
    }
    dataset.select({rows_written_, 0}, {rows, cols}).write_raw(values.data());
  }

  std::filesystem::path output_path_;
  std::size_t depth_ = 0;
  std::size_t bar_count_ = 0;
  std::size_t rows_written_ = 0;
  HighFive::File file_;
  HighFive::Group bar_group_;
  HighFive::Group book_group_;
  BarDataSets bar_;
  BookDataSets book_;
};

}  // namespace itch::tools
