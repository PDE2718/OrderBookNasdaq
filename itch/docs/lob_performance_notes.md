# LOB 性能设计备忘录

本文记录当前订单簿重构过程中关于性能、内存池、Level 队列和索引设计的阶段性讨论。它是备忘录，不代表当前代码已经全部实现。

## 1. Level 内部队列

价格档位 `Level` 内部维护 FIFO 队列是合理设计。商业级 LOB 常见做法是侵入式链表：

```cpp
struct Order {
  OrderId id;
  Price price;
  Qty qty;
  Side side;
  Order* prev;
  Order* next;
};

struct Level {
  Price price;
  Qty total_qty;
  uint32_t order_count;
  Order* head;
  Order* tail;
};
```

新增订单直接追加到 `tail`，保持 price-time priority。已知订单指针时，删除、撤单、成交都可以在 O(1) 内完成链表摘除或数量调整。

## 2. Bid / Ask 两边结构

当前推荐仍然是 bid 和 ask 各自维护一个价格树：

```cpp
std::map<Price, Level*, std::greater<Price>> bids;
std::map<Price, Level*> asks;
```

原因：

```text
best bid = bids.begin()
best ask = asks.begin()
语义清楚
允许同一 price 同时存在 bid level 和 ask level
```

同一个价格可以在 bid 和 ask 两侧同时存在。连续交易中通常不希望 best bid >= best ask，但这不等于某个价格不能同时在两侧出现。

## 3. BookOrder 是否保存 Level 引用

删除、撤单、成交消息通常只带 `order_reference_number`，不带价格和方向。处理流程是：

```text
msg.ref -> orders_by_ref[ref] -> BookOrder
```

拿到 `BookOrder` 后有两种方式定位档位：

```text
1. order->level
2. order->side + order->price -> 再查价格树
```

`Level*` 或未来的 `level_idx` 是冗余信息，但可以省掉热路径上的一次 price map 查询。代价是订单对象更大，并多一个一致性不变量。

当前 map + pointer 版本可以保留 `Level*`。如果未来转向 index pool，建议改成：

```cpp
uint32_t level_idx;
```

而不是裸指针。

## 4. 当前 ObjectPool 的特点

当前 `itch::ObjectPool<T>` 是单线程 append-only block pool：

```text
构造时分配初始 block
create = pop free slot + placement new
destroy = push free slot
耗尽时追加新 block，旧 block 不搬移
记录 high_watermark / expansion_count
```

优点：

```text
实现简单
地址稳定
适合单线程 LOB
不引入 atomic/CAS 成本
正常热路径不触发 block 分配
```

缺点：

```text
扩容点仍会触发 allocator 抖动
容量如果长期估太小，会出现多次扩容
指针字段通常是 64-bit，占用较大
```

## 5. 容量和扩容

当前实现支持耗尽时静默扩容。默认初始容量由 `BookCapacity` 控制：

```text
BookOrder pool: 4,096
Level pool:       512
```

扩容按几何 block 追加，不会移动旧对象，因此 `BookOrder*` 和 `Level*` 仍然稳定。这个策略适合 research/replay：可以从小容量起步，用 `high_watermark` 观察真实需求。

live trading 模式仍建议用历史回放得到的 watermark 设置足够大的初始容量，尽量让交易时段不发生扩容。

## 6. 是否需要回收 block

单线程 LOB 专用场景里，盘中通常不应该回收 pool 内存。

原因：

```text
回收会把 allocator、page fault、TLB/cache 扰动带回热路径
订单簿内存规模通常比延迟抖动便宜
活跃订单下降后仍可能再次上升
```

建议策略：

```text
盘中：不回收
午间：通常也保留
session 结束：reset/reuse
程序退出或 LOB 销毁：整体释放
```

如果研究环境一次回放大量 symbol 或 symbol universe 动态变化很大，可以考虑非热路径的 `release_unused_blocks()`，但不要在普通订单处理路径上做细粒度 block 回收。

## 7. Slick object pool 对比

`ref_code/object_pool.h` 中的 slick 实现是固定容量、power-of-two、lock-free MPMC 池：

```text
构造时 new T[size]
free_objects_ 保存空闲对象指针
reserved_ / consumed_ / control_ 组成无锁 ring
allocate/free 可多线程并发
池耗尽时 fallback 到 new T()
```

它的优点：

```text
支持多线程
固定容量预分配
cache-line 对齐意识
池外对象自动 delete
```

对当前 LOB 的不足：

```text
单线程热路径承担 atomic/CAS 成本
要求 T default constructible
池内 free 不逐对象析构
池耗尽 fallback 到 heap 会引入尾部抖动
实现复杂度更高
```

当前判断：slick 更适合多线程共享池；本项目 LOB 热路径更适合单线程专用池。

## 8. Index pool 方向

未来如果追求更极致的 LOB 专用性能，可以考虑 fixed-capacity index pool：

```cpp
using PoolIndex = uint32_t;
constexpr PoolIndex kInvalidIndex = UINT32_MAX;

struct BookOrder {
  uint64_t ref;
  uint32_t shares;
  uint32_t price;
  uint32_t level_idx;
  uint32_t prev_idx;
  uint32_t next_idx;
  BookSide side;
};
```

池内部可以是：

```text
slots: 连续数组或 vector<Slot>
free_indices: uint32_t 栈
allocate: pop free_indices.back()
free: push index back
```

优点：

```text
内存连续
索引比指针小
更利于 cache
调试和快照更方便
```

注意点：

```text
如果仍用裸指针，vector 扩容会导致指针失效
如果全系统用 index，vector 扩容不会破坏引用，但仍有延迟抖动
live 模式应按 watermark 设置初始容量并预热，不建议盘中扩容
```

对单个 symbol LOB，`uint32_t` index 非常够用。`2^32 - 1` 个 slot 已经远超正常交易场景中单个订单簿的活跃订单规模。

## 9. 订单号索引

当前订单号到订单对象的映射使用 header-only 的 `ankerl::unordered_dense::map`：

```cpp
ankerl::unordered_dense::map<uint64_t, BookOrder*> orders_;
```

它是 flat / dense hash map。和标准库 node-based `std::unordered_map` 相比，它更适合当前 LOB 场景：

```text
没有每个元素一个 node 的分配模式
数据更连续
reserve(initial_orders) 后更适合低抖动热路径
依赖轻，只 vendored 头文件
```

当前仍然保存 `BookOrder*`，因为订单和档位池还没有切换到 index API。未来如果改成 fixed index pool，可以把 value 改成 `uint32_t order_idx`。

## 10. 后续可考虑的具体改造

短期：

```text
把 ObjectPool 耗尽转换成 ApplyStatus::CapacityExceeded
增加更多 LOB 级容量诊断输出
```

中期：

```text
区分 research mode 和 live mode
research mode 允许增长
live mode 预热后禁止增长，容量不足显式报错
```

长期：

```text
尝试 fixed-capacity index pool
BookOrder/Level 内部链接从指针改为 uint32_t index
orders_by_ref 从 ref -> BookOrder* 改成 ref -> order_idx
```

## 11. 快照和订单簿解耦

当前已经把盘口快照从 LOB 内部抽出来：

```text
LimitOrderBook
  -> fill_bid_levels(std::span<LevelSnapshot>)
  -> fill_ask_levels(std::span<LevelSnapshot>)
  -> BookSnapshotBuffer
  -> L2SnapshotView / BarSnapshotView
  -> tool-local SnapshotBatch + SnapshotH5Writer / other output backends
```

这里的关键点是：LOB 不返回 `std::vector`，而是填充调用方提供的 `std::span`，然后由 `BookSnapshotBuffer` 维护固定深度的复用缓冲区。这样每次采样不会因为构造临时 vector 产生额外分配，也让 HDF5、CSV、Python bridge 或在线指标计算都可以复用同一份轻量视图。

`BarL2SnapshotCollector` 管时间桶和 K 线聚合，当前 `tools/snapshot_h5_writer.hpp` 里的 `SnapshotBatch` 和 `SnapshotH5Writer` 用 HighFive 管 HDF5 落盘格式。订单簿本身不依赖 HDF5，这样后面替换为 Arrow/Parquet/Zarr，或者把快照直接喂给策略，都不需要改 LOB 热路径。
