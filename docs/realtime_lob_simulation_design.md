# 实时 LOB 重构模拟项目设计备忘录

本文记录一个未来新项目的设计想法：用本地 ITCH 数据模拟实时网络流，在线重构大量股票的限价订单簿，并支持实时查询盘口信息。

当前不开始实现，只先记录方案、难点和推荐路线。

## 1. 项目目标

目标不是再做一个离线回放工具，而是把现有 `itch` 里的解析和 LOB 能力放进一个接近实时系统的框架：

```text
本地 ITCH BinaryFILE
  -> replay/feed simulator
  -> 网络传输或进程内队列
  -> realtime book builder
  -> snapshot publisher
  -> query API
```

希望验证的问题包括：

```text
1. 能否用本地历史数据模拟实时 feed
2. 能否全量重构上千只股票的 LOB
3. 能否在重构时低延迟查询 best bid/ask 和 L2
4. 单机多线程是否足够，什么时候需要多进程或多机器
```

## 2. 可复用组件

现有 `itch` 项目里大部分核心组件可以直接复用：

```text
messages.hpp
message_types.hpp
message_accessors.hpp
parser.hpp / parser.cpp
order_book.hpp / order_book.cpp
object_pool.hpp
book_snapshot.hpp
l2_snapshot.hpp
fixed_ascii.hpp
endian.hpp
types.hpp
```

需要新写或轻微抽象的部分：

```text
SocketFrameReader       从 socket 读 frame，接口可模仿 FrameReader
ReplayClock             把 ITCH timestamp 映射到 wall clock
FeedSimulator           按速度回放历史数据
FrameProtocol           定义网络帧格式
BookWorker              每个 worker 独占若干 LOB
SnapshotStore           查询层读取的最新盘口快照
QueryServer             HTTP/TCP/gRPC 查询接口
```

设计原则仍然是：不把网络、查询、HDF5、Python 等外层能力塞进 `LimitOrderBook`。

## 3. 第一版整体架构

推荐第一版采用“单进程多线程”的架构：

```text
feed receiver / dispatcher thread
  -> worker_queue[0]
  -> worker_queue[1]
  -> ...
  -> worker_queue[N-1]

book_worker[0]
  -> owns many LimitOrderBook
book_worker[1]
  -> owns many LimitOrderBook
...

snapshot publisher / query server
  -> reads latest immutable snapshots
```

更具体地：

```text
1. receiver 从 socket 或本地 replay source 收到原始 ITCH frame
2. receiver 只解析公共头里的 type / stock_locate / timestamp
3. receiver 根据 stock_locate 把 frame 放入某个 worker 的队列
4. worker 解析完整 Message，并更新自己负责的 LOB
5. worker 定期发布不可变快照
6. query server 只读快照，不直接读正在变化的 LOB
```

## 4. 为什么不是每只股票一个线程

全量 ITCH 涉及上千只股票。如果每只股票一个线程，会有明显问题：

```text
线程数太多
调度开销大
cache locality 差
线程之间负载极不均衡
查询和监控更复杂
```

更合理的是：

```text
每个线程维护一组 LOB
worker 数量接近 CPU core 数量
同一只股票永远只归一个 worker
```

这样能保证：

```text
单个 LOB 仍然单线程写
无需在 LOB 内部加锁
跨股票天然并行
```

## 5. 分片策略

最简单的分片方式：

```cpp
worker_id = stock_locate % num_workers;
```

优点：

```text
实现简单
无需提前知道全部 symbol
稳定可重复
```

缺点：

```text
无法处理热点股票分布不均
大票可能碰巧落到同一个 worker
```

更推荐的生产化方式是维护一张映射表：

```cpp
locate_to_worker[stock_locate] = worker_id;
```

这张表可以有三种生成方式：

```text
1. 简单 hash：stock_locate % N
2. 根据历史消息量预先均衡分配
3. 手动把 AAPL/NVDA/TSLA/MSFT 等大票错开
```

第一版可以先用 hash，后续再加可配置映射。

## 6. 网络收到数据后如何分发

receiver 不应该完整做所有解析和 LOB 更新，否则它会变成瓶颈。推荐它只做轻量工作：

```text
1. 从 socket 读完整 frame
2. 检查 length / sequence
3. 读取 body[0] 的 message type
4. 读取公共头里的 stock_locate
5. 入队到对应 worker
```

ITCH 常见消息公共头是：

```text
offset 0: type
offset 1: stock_locate
offset 3: tracking_number
offset 5: timestamp
```

`UnknownMessage` 或无股票归属的系统级消息，可以按规则处理：

```text
SystemEvent(S) 广播到所有 worker
Malformed/Unknown 进入监控或错误队列
StockDirectory(R) 正常按 stock_locate 分发，同时可更新 symbol 映射
```

同一只股票的所有消息必须进入同一个 worker，并保持顺序。不同股票之间的全局顺序通常不需要在 LOB 层保持，因为每个 LOB 是独立状态机。

## 7. 队列设计

如果只有一个 receiver，一个 worker 一个队列，那么拓扑是：

```text
single producer -> single consumer
```

因此每个 worker 可以使用 SPSC ring buffer：

```text
receiver -> SPSCQueue[worker_id] -> worker
```

队列元素推荐第一版传原始 frame，而不是已经解析好的 `Message`：

```cpp
struct FeedFrame {
  u64 sequence;
  u64 receive_time_ns;
  u16 length;
  std::array<u8, kMaxItchMessageLength> body;
};
```

这样做的优点：

```text
receiver 很轻
worker 复用现有 parser
消息生命周期清楚
跨机器协议不受 C++ struct padding 影响
```

缺点是每条消息会 copy 一次 body，但 ITCH 单条消息很小，第一版可以接受。等确认 receiver 成为瓶颈后，再考虑零拷贝 buffer pool。

## 8. 查询线程不直接读 LOB

多线程下最不建议的做法是：

```text
query thread 直接读 LimitOrderBook
worker thread 同时写 LimitOrderBook
```

这会引入锁、读写一致性和尾延迟问题。

更推荐：

```text
worker 单线程更新 LOB
worker 定期发布 immutable snapshot
query server 只读最新 snapshot
```

例如每个 worker 每 10ms 或每 N 条消息发布一次：

```cpp
struct PublishedBook {
  Symbol symbol;
  u64 event_timestamp_ns;
  u64 publish_time_ns;
  u32 best_bid;
  u32 best_ask;
  std::array<LevelSnapshot, Depth> bids;
  std::array<LevelSnapshot, Depth> asks;
};
```

查询语义变成：

```text
返回最近一次发布的盘口快照
```

这对于监控、可视化和大多数实时研究查询是合理的。真正需要“每条消息后严格最新”的场景，可以另开专门的同步查询机制，但不建议作为第一版默认路径。

## 9. 多线程还是多进程

### 9.1 第一版推荐多线程

单进程多线程的优点：

```text
实现简单
共享内存方便
查询聚合简单
队列延迟低
更容易复用现有 C++ 组件
```

缺点：

```text
单进程故障会影响全部 worker
NUMA / CPU 绑定需要后续仔细做
进程内内存可能非常大
```

### 9.2 多进程适合后续生产化

多进程的优点：

```text
故障隔离更好
可以跨机器扩展
可以按 shard 独立部署和监控
更适合 NUMA 或多网卡环境
```

代价：

```text
IPC 复杂
查询聚合复杂
部署、日志、监控复杂
全局配置和服务发现复杂
```

推荐路线：

```text
MVP: 单进程多线程
进阶: 多进程，每个进程内部仍然多线程
更进一步: 多机器 shard，统一 query gateway
```

## 10. Feed simulator 设计

模拟器可以支持几种模式：

```text
burst      不 sleep，尽快发送，用于压力测试
realtime   按 ITCH timestamp 原速发送
speed=N    N 倍速回放
```

时间映射：

```text
第一条事件 timestamp = t0
开始发送 wall time = w0
第 i 条消息目标发送时间 = w0 + (ts_i - t0) / speed
```

注意盘前 warmup：

```text
盘前订单消息应该用于构建 09:30 开盘时的 book
但可以选择只从 09:30 开始对外发布查询快照
```

因此模拟器或 book builder 可以区分：

```text
warmup_from
publish_from
publish_until
```

## 11. 网络协议建议

不建议把 C++ `Message` struct 序列化上网。更推荐传原始 ITCH body：

```text
frame header
  magic/version
  sequence_number
  send_time_ns
  payload_len
payload
  raw ITCH message body
```

原因：

```text
跨语言稳定
不受 C++ padding 影响
接收端继续复用 parser
更接近真实 feed
```

TCP 第一版足够。UDP/multicast 更接近真实行情，但会引入丢包、重传、gap fill 等新问题，不适合第一版。

## 12. 背压和错误策略

队列满时必须有明确策略。第一版建议：

```text
realtime 模式：阻塞 sender 或 dispatcher，并记录 backpressure
burst 压测模式：可以选择阻塞或直接失败
不要默认静默丢消息
```

需要记录的指标：

```text
每个 worker 的 queue depth
每个 worker 的 processed messages
每个 worker 的 apply errors
每个 symbol 的 live orders / levels
receiver 收到的 sequence
worker 处理到的 sequence
最大延迟 / 平均延迟
```

## 13. 容量和内存难点

当前 `LimitOrderBook` 默认初始容量：

```text
orders: 4,096
levels:   512
```

这个默认更适合本地 research/replay：冷门股票不会占用过多内存，活跃股票会在耗尽时追加 block。扩容点仍然会有 allocator 抖动，所以 live trading 模式不应该依赖盘中扩容。

全量系统需要：

```text
按 symbol 或 liquidity tier 配置容量
大票给大容量
小票给小容量
记录 high watermark
容量不足时明确报错
```

第一版可以先分三档：

```text
large:  AAPL/MSFT/NVDA/TSLA 等
medium: 普通活跃股
small:  低活跃股票
```

或者先做全量 replay 统计，估算每只股票的最大 live orders / levels，再反推容量配置。

## 14. Linux 服务器的使用方式

有一台可 ssh 的 Linux 服务器时，可以做两种实验。

### 14.1 服务器做 feed source

```text
Linux server:
  replay_itch_feed --input full.bin --bind 0.0.0.0:9001 --speed 10

Local or another machine:
  realtime_book_server --connect server:9001 --workers 16
```

适合测试网络传输、跨机器时钟、吞吐和背压。

### 14.2 服务器做 book builder

```text
Linux server:
  realtime_book_server --input full.bin --workers 32 --query-port 8080

Local:
  query_book --host server --symbol NVDA --depth 10
```

适合利用服务器更多 CPU 和内存。

如果要测端到端延迟，服务器和本地最好保证 NTP 同步。

## 15. 推荐 MVP 路线

### 阶段 1：单机进程内模拟

```text
File replay source
Dispatcher
N 个 BookWorker
SnapshotStore
简单 CLI 查询
```

目标：验证分片、worker、快照发布和查询模型。

### 阶段 2：本机 TCP

```text
replay_itch_feed
realtime_book_server
query_book
```

目标：验证网络 frame protocol 和实时回放速度控制。

### 阶段 3：跨机器 TCP

```text
Linux server feed
local/server book builder
```

目标：验证吞吐、延迟、背压、部署脚本。

### 阶段 4：全量容量规划

```text
先离线统计每只 symbol 的 live order / level watermark
再生成容量配置
再做全量实时模拟
```

目标：用离线 watermark 反推每只股票或每组股票的合理初始容量，避免全量系统在内存和扩容抖动之间盲选。

## 16. 当前最关键的设计判断

当前建议保持这些原则：

```text
1. 同一 LOB 单线程写
2. 多个 LOB 按 stock_locate 分片到 worker
3. receiver 不做重活，只切帧和分发
4. worker 自己解析 Message 并应用 LOB
5. query 只读发布快照，不直接读活 LOB
6. 第一版用 TCP，不碰 UDP/multicast gap recovery
7. 第一版单进程多线程，后续再做多进程/多机器
8. 全量前先做容量统计，不盲目依赖默认初始容量
```

## 17. 后续待定问题

真正开始实现前，需要进一步决定：

```text
1. 第一版查询接口用 HTTP、TCP text protocol，还是 CLI-only
2. 是否引入现成 SPSC queue 库，还是自己写固定容量 ring
3. worker 数量如何配置，是否支持 CPU affinity
4. locate_to_worker 是否用历史负载均衡表
5. PublishedBook 的 depth 是否固定编译期，还是运行期配置
6. snapshot 发布频率按时间、消息数，还是两者结合
7. 全量 LOB 容量配置从哪里生成和加载
8. Linux 服务器优先做 feed source 还是 book builder
```

这份 memo 的结论是：先做单进程多线程 shard 架构，保持 LOB 单写，查询走快照。这个方案最能复用现有 `itch` 组件，同时不会一开始就把问题推到复杂分布式系统。
