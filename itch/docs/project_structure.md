# itch 项目结构说明

`itch/` 是 C++20 核心子项目，目录结构如下：

```text
itch/
  CMakeLists.txt
  include/        公共头文件：messages、parser、order_book、snapshot、object_pool
  src/            核心库实现：frame_reader、parser、formatter、order_book
  tools/          parse_messages、filter、export_l2_snapshots
  tests/          order_book_tests
  docs/           专题设计文档
```

主数据流：

```text
BinaryFILE
  -> FrameReader
  -> parse_message_body
  -> Message(std::variant)
  -> to_book_message
  -> LimitOrderBook::apply(BookMessage)
  -> BookSnapshotBuffer
  -> BarL2SnapshotCollector
  -> SnapshotH5Writer
  -> HDF5
```

核心边界：

```text
parser/message       不知道订单簿
LimitOrderBook       不知道 HDF5、Python、文件输出
BookSnapshotBuffer   只负责从 LOB 生成固定深度 L2 视图
collector            只做时间桶和 bar 聚合
writer/tool          才知道具体落盘格式
```

完整构建、数据下载、filter、HDF5 导出和 Python 画图流程见仓库根目录 [README.md](../../README.md)。
