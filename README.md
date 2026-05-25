# OrderBookNasdaq

这是一个围绕 Nasdaq TotalView-ITCH 5.0 做的订单簿学习项目。最初只是想把 ITCH 报文读明白，后来逐步补上了按股票裁剪数据、重构单股票订单簿、导出 L2 快照和用 Python 画图这些环节。现在它基本形成了一条完整的小研究流水线：从一份历史 ITCH BinaryFILE 出发，最后得到分钟级 K 线和盘口深度热力图。

它不是生产级交易系统，也没有假装自己是。这个项目更像是一份工程化练习笔记：我尽量让代码边界清楚、数据口径说得明白，同时在对象池、订单索引、快照导出这些地方保留一些接近真实系统会关心的性能意识。

## 项目长什么样

```text
OrderBookNasdaq/
  itch/                  C++20 核心项目：parser、filter、LOB、HDF5 exporter
  itch-playground/       Python 可视化脚本和本地生成结果目录
  docs/                  协议规则与实时化设计备忘录
  references/            数据来源说明和规范链接；本地 PDF/TXT 不提交
  data/                  本地行情数据目录；raw/filtered 内容不提交
```

主流程很直接：

```text
ITCH BinaryFILE
  -> itch/tools/filter
  -> data/filtered/test.bin
  -> itch/tools/export_l2_snapshots
  -> itch-playground/data/l2_hdf5_60s/SYMBOL/snapshot.h5
  -> itch-playground/scripts/plot_orderbook_l2.py
  -> itch-playground/figures/SYMBOL_l2_60s.png
```

根目录只放项目入口、轻量文档和目录占位。原始数据、filter 后的数据、HDF5 和图片都留在本地，不进 Git。

## 设计想法

我比较在意的一点是：不要让一个模块知道太多事情。订单簿只应该知道订单簿消息，HDF5 writer 不应该混进 LOB，Python 画图也不应该倒逼 C++ 里的数据结构。

现在的边界大概是这样：

```text
FrameReader             只负责 BinaryFILE 切帧
parser/messages         只负责把 wire format 解析成 Message variant
message_accessors       负责轻量访问和 Message -> BookMessage 路由
LimitOrderBook          只消费 BookMessage，不知道 HDF5、Python、StockDirectory
BookSnapshotBuffer      只从 LOB 抓固定深度 L2 视图
BarL2SnapshotCollector  只负责时间桶和 OHLCV 聚合
SnapshotH5Writer        只负责 HDF5 schema 和批量写盘
```

`Message` 是解析层的全量 ITCH 消息；`BookMessage` 是订单簿真正能处理的那一小部分消息。比如 `StockDirectory` 应该用于建立 symbol 和 locate 的映射，而不是被直接塞给 LOB 然后默默 ignored。这个分层让错误更早暴露，也让后面加别的消费逻辑更自然。

LOB 内部目前比较朴素：

```text
Bid/Ask 两棵 price map
Level 内部 FIFO intrusive list
OrderRef -> BookOrder* flat hash map
BookOrder / Level append-only ObjectPool
```

对象池不是追求极限低延迟的 fixed arena，而是更适合本地 replay 的 growable pool：先用小容量起步，耗尽时追加 block，旧 block 不移动，所以指针稳定。每次导出结束都会打印 watermark，方便知道一只股票一天大概需要多少 live orders 和 price levels。

## 已经能做什么

- 读取 ITCH BinaryFILE：`2-byte length + message body`
- 把主要 ITCH 5.0 消息解析成 C++ struct 和 `std::variant`
- 打印/检查解析结果，跑基础 parse smoke test
- 按 symbol/locate 从完整 ITCH 文件裁剪 mini feed，并保留系统通用消息
- 重构单股票 LOB：add、execute、execute-with-price、cancel、delete、replace
- 生成 L2 快照：best bid/ask、若干档 price/shares/orders
- 按 regular session 时间桶聚合 OHLC、volume、notional、has_trade
- 每只股票导出一个 HDF5：`SYMBOL/snapshot.h5`
- 用 Python 批量画价格、成交量和 bid/ask depth heatmap

## 数据从哪里来

历史 ITCH 数据可以从 Nasdaq 官方入口获取：

<https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/>

这个项目使用的示例文件名是：

```text
data/raw/01302020.NASDAQ_ITCH50.bin
```

TotalView-ITCH 5.0 规范可以从 Nasdaq Trader 下载：

<https://nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf>

本地目录约定：

```text
data/raw/       完整原始 ITCH 文件，本地保存
data/filtered/  filter 生成的 mini feed，本地保存
references/     数据源说明；PDF/TXT 可本地放置但默认忽略
```

这些文件通常很大，或者属于外部下载资料，所以默认都不会提交到 Git。

## 编译

依赖大致是这些：

- CMake 3.20+
- C++20 编译器
- HDF5 C/C++ 开发库
- HighFive
- Python 环境：`h5py`、`numpy`、`matplotlib`

HighFive 的处理方式比较灵活。CMake 会按这个顺序来：

```text
1. find_package(HighFive)
2. -DHIGHFIVE_SOURCE_DIR=/path/to/highfive 指向本地源码缓存
3. FetchContent 从固定 tag v3.3.0 拉取
```

如果你本机已经有 HighFive 源码：

```bash
cd itch
cmake -S . -B build -DHIGHFIVE_SOURCE_DIR=/path/to/highfive
cmake --build build -j
ctest --test-dir build --output-on-failure
```

如果想让 CMake 自己拉依赖：

```bash
cd itch
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

如果网络访问 GitHub 比较慢，建议先用包管理器安装 HighFive，或者提前 clone HighFive 后通过 `HIGHFIVE_SOURCE_DIR` 指过去。

## 从原始数据跑出图

先把完整 ITCH 文件放到：

```text
data/raw/01302020.NASDAQ_ITCH50.bin
```

生成一个多股票 mini feed：

```bash
cd itch
./build/filter \
  --input ../data/raw/01302020.NASDAQ_ITCH50.bin \
  --output ../data/filtered/test.bin \
  --symbols NVDA,AAPL,MSFT,GOOGL,AMZN,FB,TSLA,INTC
```

备注：2020-01-30 这天 Meta 还叫 `FB`。

导出 60 秒 L2/K 线 HDF5：

```bash
./build/export_l2_snapshots \
  --input ../data/filtered/test.bin \
  --symbols NVDA,AAPL,MSFT,GOOGL,AMZN,FB,TSLA,INTC \
  --output ../itch-playground/data/l2_hdf5_60s \
  --interval 60s \
  --depth 10 \
  --initial-orders 1024 \
  --initial-levels 128
```

导出完成时，会看到每只股票的对象池水印，例如：

```text
order_pool_capacity=32768 order_pool_high_watermark=30275 order_pool_expansions=5
level_pool_capacity=8192 level_pool_high_watermark=5482 level_pool_expansions=6
```

最后画图：

```bash
cd ../itch-playground
source "$(conda info --base)/etc/profile.d/conda.sh"
conda activate MLbase

python scripts/plot_orderbook_l2.py \
  --input-root data/l2_hdf5_60s \
  --output-dir figures
```

输出会在：

```text
itch-playground/figures/AAPL_l2_60s.png
itch-playground/figures/AMZN_l2_60s.png
...
```

## 文档

推荐阅读顺序：

- [ITCH 规则中文解读](docs/itch50_rules.md)
- [消息对象模型](itch/docs/messages_design.md)
- [ObjectPool 设计](itch/docs/object_pool_design.md)
- [LOB 性能设计备忘录](itch/docs/lob_performance_notes.md)
- [HDF5 L2 快照导出](itch/docs/hdf5_snapshot_export.md)
- [中英术语对照表](docs/glossary.md)
- [实时 LOB 模拟设计草案](docs/realtime_lob_simulation_design.md)

## Git 和大文件

`.gitignore` 已经排除了这些本地内容：

```text
.bkp/
.ref_projects/
.vscode/
data/raw/
data/filtered/
itch/build/
itch/logs/
itch-playground/data/
itch-playground/figures/
*.bin, *.gz, *.h5, *.png, *.pdf
```

所以 fork 之后，仓库里应该只留下源码、脚本、轻量文档和目录占位，不会把 12GB 原始 ITCH 数据、filter 后的数据、HDF5 或生成图片推到远程。
