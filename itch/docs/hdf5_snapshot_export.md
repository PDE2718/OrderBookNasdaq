# HDF5 L2 快照导出设计

本文说明 `export_l2_snapshots` 的输出语义、HDF5 schema，以及 Python 侧如何读取和画图。本文内容已经按当前版本代码校对过，和 `BookSnapshotBuffer + BarL2SnapshotCollector + SnapshotH5Writer` 这条实际链路一致。

## 1. 目标

导出工具面向研究和可视化，不放在 LOB 核心热路径里。当前链路是：

```text
LimitOrderBook
  -> BookSnapshotBuffer
  -> L2SnapshotView
  -> BarL2SnapshotCollector
  -> BarSnapshotView
  -> SnapshotBatch
  -> SnapshotH5Writer
```

也就是说：

```text
LOB 负责状态
BookSnapshotBuffer 负责抓盘口快照
collector 负责时间桶和 OHLCV
writer 负责 HDF5 schema
```

数据缓冲和 HDF5 写文件由 `tools/snapshot_h5_writer.hpp` 里的 `SnapshotBatch` 和 `SnapshotH5Writer` 负责。它们使用 HighFive 写 HDF5，不再维护自定义 HDF5 C API wrapper header。

当前输出一只股票一个 HDF5 文件：

```text
OUTPUT_ROOT/
  SYMBOL/
    snapshot.h5
```

这样 Python、Julia 或其他研究脚本可以按 symbol 独立加载，避免一个大文件里混太多对象。

## 2. 时间语义

当前 regular session 固定为：

```text
09:30:00 - 16:00:00
session_ns = 23,400,000,000,000
```

`--interval` 支持整数单位：

```text
ns, us, ms, s, m
```

约束：

```text
最小 1ms
必须能整除 regular session 长度
不支持浮点 interval
```

每个 bar 的语义是 `(start, end]`。L2 快照是处理完该 `end` 时间点之前所有相关消息后的盘口状态。

盘前订单簿消息会被应用到 LOB，因此开盘第一根 bar 的 book 不是从空簿开始。K 线成交聚合只统计 regular session 内的可见成交。

## 3. 成交聚合口径

当前 `volume_policy` 为：

```text
E + printable C
```

含义：

```text
OrderExecuted(E): 计入，价格来自被执行订单当前价格
OrderExecutedWithPrice(C): 仅 printable == 'Y' 时计入，价格使用 execution_price
NonCrossTrade(P): 当前不计入
CrossTrade(Q): 当前不计入
```

因此这里的 `volume` 是“明盘影响订单簿成交”的研究口径，不一定等于行情网站的 consolidated daily volume。

## 4. Schema

根属性：

```text
schema            string  "itch.l2_snapshot.v1"
schema_version    u32
symbol            string
session_start_ns  u64
session_end_ns    u64
interval_ns       u64
bar_count         u64
depth             u64
price_scale       u32    当前为 10000
volume_policy     string
bar_semantics     string
```

`/bar` group：

```text
start_ns    u64 [bar_count]
end_ns      u64 [bar_count]
open_px     u32 [bar_count]
high_px     u32 [bar_count]
low_px      u32 [bar_count]
close_px    u32 [bar_count]
volume      u64 [bar_count]
notional    u64 [bar_count]
has_trade   u8  [bar_count]
```

`/book` group：

```text
best_bid    u32 [bar_count]
best_ask    u32 [bar_count]
bid_px      u32 [bar_count, depth]
bid_shares  u64 [bar_count, depth]
bid_orders  u32 [bar_count, depth]
ask_px      u32 [bar_count, depth]
ask_shares  u64 [bar_count, depth]
ask_orders  u32 [bar_count, depth]
```

价格字段为 ITCH 原始整数价格，真实价格为：

```text
price = raw_price / price_scale
```

空档位使用 0 填充。

## 5. 导出命令

```bash
cd itch
cmake -S . -B build
cmake --build build -j

./build/export_l2_snapshots \
  --input ../data/filtered/test.bin \
  --symbols NVDA,AAPL,MSFT,GOOGL,AMZN,FB,TSLA,INTC \
  --output ../itch-playground/data/l2_hdf5_60s \
  --interval 60s \
  --depth 10 \
  --initial-orders 1024 \
  --initial-levels 128
```

多只股票可以用逗号分隔：

```bash
--symbols NVDA,AAPL,MSFT,GOOGL,AMZN,FB,TSLA,INTC
```

## 6. Python 读取

```python
from pathlib import Path
import h5py

path = Path("data/l2_hdf5_60s/AAPL/snapshot.h5")
with h5py.File(path, "r") as f:
    symbol = f.attrs["symbol"]
    scale = int(f.attrs["price_scale"])
    close = f["bar/close_px"][:] / scale
    bid_px = f["book/bid_px"][:] / scale
    bid_shares = f["book/bid_shares"][:]
```

已有画图脚本：

```bash
cd itch-playground
source "$(conda info --base)/etc/profile.d/conda.sh"
conda activate MLbase

python scripts/plot_orderbook_l2.py \
  --input data/l2_hdf5_60s/AAPL/snapshot.h5 \
  --output figures/AAPL_l2_60s.png
```

## 7. 依赖

C++ 侧使用 HighFive 写 HDF5。CMake 会先 `find_package(HighFive)`；如果没有系统包，可以用 `-DHIGHFIVE_SOURCE_DIR=/path/to/highfive` 指向本地源码缓存；两者都没有时，CMake 通过 FetchContent 从固定 tag `v3.3.0` 拉取。

Python 侧需要：

```text
h5py
numpy
matplotlib
```

当前 `MLbase` 环境已经安装了 `h5py`。

## 8. 当前 playground 配套脚本

当前仓库里真实存在的 Python 画图脚本是：

```text
itch-playground/scripts/plot_orderbook_l2.py
```

它会读取：

```text
/bar/end_ns
/bar/close_px
/bar/high_px
/bar/low_px
/bar/volume
/bar/has_trade
/book/best_bid
/book/best_ask
/book/bid_shares
/book/ask_shares
```

并画出：

```text
best bid / best ask / close
volume bar
bid liquidity heatmap
ask liquidity heatmap
```
