# ObjectPool 设计说明

本文说明 `include/object_pool.hpp` 中当前的 `itch::ObjectPool<T>`。它是一个面向单线程 LOB 的可增长对象池，目标是在热路径保持 O(1) pop/push，同时允许 research/replay 阶段从较小初始容量起步。

## 1. 设计目标

当前订单簿热路径会频繁创建和释放：

```text
BookOrder
Level
```

这些对象有几个特点：

```text
类型固定
数量上限可以通过历史回放估计
需要稳定地址，因为 map 和 FIFO 链表保存指针
单个 LOB 默认单线程处理
```

因此当前对象池选择 append-only block 设计：

```text
构造时分配一个初始 block
容量不足时追加新 block，旧 block 永远不移动
create/destroy 在有空闲 slot 时都是 O(1)
扩容只发生在 free list 耗尽的冷路径
```

## 2. 内存布局

池内部由若干个稳定地址的 block 和一个空闲 slot 栈组成：

```cpp
struct Block {
  Slot* storage;
  uint32_t capacity;
};

Block* blocks_;
Slot** free_slots_;
```

`Slot` 是未初始化原始存储：

```cpp
struct alignas(alignof(T)) Slot {
  unsigned char data[sizeof(T)];
};
```

这样 block 预分配时不会调用 `T` 的默认构造函数。真正分配对象时，才用 placement new 在对应 slot 上构造 `T`。

`free_slots_` 是空闲 slot 指针栈。每个新 block 加入时倒序压入：

```text
&storage[capacity-1], ..., &storage[0]
```

这样第一次分配弹出的 slot 是 `storage[0]`，之后是 `storage[1]`、`storage[2]`，冷启动访问顺序是连续向前的。

当前实现仍使用裸指针管理元数据，避免把 vector 的语义带进核心组件。元数据数组只会在初始化或扩容冷路径增长；对象 block 一旦创建就不会搬移，因此外部保存的 `T*` 地址稳定。

## 3. create / destroy

分配流程：

```text
1. 检查 free_count_ 是否为 0
2. 如果耗尽，追加一个新 block
3. slot = free_slots_[--free_count_]
4. address = slot->data
5. placement new T(args...)
6. 更新 live_count / high_watermark
7. 返回 T*
```

释放流程：

```text
1. nullptr 直接返回
2. debug 模式检查指针属于某个 block
3. free_slots_[free_count_++] = slot
4. live_count_--
```

对应当前接口：

```cpp
T* create(Args&&... args);
void destroy(T* object) noexcept;
```

`ObjectPool<T>` 要求 `T` 是 trivially destructible。这个约束让释放路径不需要调用析构函数，也避免 pool 析构时扫描 live object。对 `BookOrder` 和 `Level` 这类 LOB 小对象，这是合适的约束。

当前还提供：

```cpp
capacity()
available()
live_count()
high_watermark()
expansion_count()
block_count()
```

用于容量检查、调试统计和后续全市场容量规划。

## 4. 初始容量和扩容

`ObjectPool<T>` 的默认初始容量是 4,096。`LimitOrderBook` 通过 `BookCapacity` 显式区分订单和档位：

```cpp
struct BookCapacity {
  uint32_t initial_orders = 4'096;
  uint32_t initial_levels = 512;
};
```

当前默认：

```text
BookOrder pool: 4,096
Level pool:       512
```

当空闲 slot 耗尽时，pool 会追加一个新 block。追加大小按几何增长：

```text
initial, initial, 2*initial, 4*initial, ...
```

例如初始 1,024 个订单 slot，连续扩容后总容量会变成：

```text
1,024 -> 2,048 -> 4,096 -> 8,192 -> 16,384
```

这样小股票不会预分配过多内存，活跃股票也只会在早期经历少数几次扩容。

## 5. 地址稳定性

每个 block 创建后不再搬移，因此 `T*` 地址稳定。订单簿可以安全保存：

```cpp
ankerl::unordered_dense::map<uint64_t, BookOrder*> orders_
Level::first/last  -> BookOrder*
BookOrder::prev/next
BookOrder::level
```

这是当前暂不切换 index 方案的前提。

## 6. 容量不足策略

如果池耗尽，当前实现会静默追加 block。只有系统内存真的不足，或者总容量超过 `uint32_t` 可表达范围时，才会抛出：

```cpp
throw std::bad_alloc();
```

这不是 fallback 到每对象 `new/delete`，而是追加一个批量 block。它仍然会在扩容点带来一次 allocator 抖动，但扩容后热路径恢复为 pop/push。

live trading 场景仍建议用历史 watermark 明确设置足够大的初始容量，把扩容压到启动或预热阶段。research/replay 场景可以从小容量起步，通过 `high_watermark()` 和 `expansion_count()` 观察真实需求。

## 7. 当前约束

当前池子有意保持简单：

```text
要求 T 是 trivially destructible
不是线程安全的
不做 release 模式 double free 检查
release 模式下不做强校验；debug 模式通过 assert 检查明显错误
支持耗尽时追加 block，但不保证扩容点低延迟
不做 block 回收
```

debug 模式下通过 `assert` 检查指针范围、slot 对齐和 double free 的明显情况。

## 8. 和后续 index pool 的关系

当前池子仍返回 `T*`，因为订单簿内部暂时保留指针链表。

未来如果切换到 index pool，可以把接口改成：

```text
allocate -> uint32_t index
get(index) -> T&
deallocate(index)
```

然后订单簿内部用：

```text
prev_idx
next_idx
level_idx
orders_by_ref: ref -> order_idx
```

这会进一步减少对象大小，并避免裸指针依赖。但改动面更大，所以当前先保留 pointer pool。
