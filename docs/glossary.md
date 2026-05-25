# 中英术语对照表

这张表只覆盖本项目文档和代码里反复出现的核心概念，方便在 ITCH 规范、C++ 代码和中文说明之间来回对照。

| 中文 | English | 备注 |
|---|---|---|
| 限价订单簿 | Limit Order Book, LOB | 项目里的 `LimitOrderBook` |
| 订单簿重构 | Order Book Reconstruction | 从 ITCH 增量消息恢复 LOB 状态 |
| 报文 | Message | 解析后的 `Message` variant 或原始 ITCH 消息 |
| 订单簿消息 | Book Message | 能改变 LOB 的 `BookMessage` |
| 原始线格式 | Wire Format | ITCH 二进制字段布局 |
| 数据帧 | Frame | BinaryFILE 中的 `2-byte length + body` |
| 股票定位码 | Stock Locate | ITCH 当日 symbol 索引 |
| 股票目录消息 | Stock Directory | `R` 消息，建立 symbol/locate 映射 |
| 系统事件 | System Event | `S` 消息，开收盘等阶段信息 |
| 买盘 | Bid | 买方价格档位 |
| 卖盘 | Ask | 卖方价格档位 |
| 最优买价 | Best Bid | 买一价 |
| 最优卖价 | Best Ask | 卖一价 |
| 档位 | Level | 单侧某个价格上的 FIFO 队列和聚合信息 |
| 先进先出 | First In First Out, FIFO | 同价位内的时间优先队列 |
| 订单引用号 | Order Reference Number, OrderRef | ITCH 中定位订单的 id |
| 新增订单 | Add Order | `A` / `F` 消息 |
| 成交 | Execution | `E` / `C` 消息 |
| 撤单 | Cancel | `X` 消息，减少数量 |
| 删除订单 | Delete | `D` 消息，移除订单 |
| 替换订单 | Replace | `U` 消息，旧 ref 失效，新 ref 生效 |
| 明盘成交 | Visible / Printable Trade | 当前 volume 口径使用 `E + printable C` |
| 快照 | Snapshot | 某个时间点的 L2 盘口状态 |
| 二档行情 / 深度行情 | Level 2, L2 | 多档 bid/ask 聚合盘口 |
| K 线 | OHLC Bar | open/high/low/close/volume 时间桶 |
| 常规交易时段 | Regular Session | 当前固定为 09:30-16:00 |
| 对象池 | Object Pool | `ObjectPool<T>` |
| 追加式块池 | Append-only Block Pool | 耗尽时追加 block，旧 block 不移动 |
| 高水位 | High Watermark | 历史最大 live 对象数 |
| 静默扩容 | Silent Expansion | pool 耗尽时自动追加 block |
| 小型数据集 | Mini Feed | filter 生成的多股票裁剪文件 |
| 批量写盘 | Batch Write | `SnapshotBatch` 到 HDF5 |
| 热路径 | Hot Path | 高频执行、需控制抖动的代码路径 |
| 回放 | Replay | 用历史数据按顺序驱动系统 |
| 合并市场成交量 | Consolidated Volume | 跨市场统计口径，不等于本项目 volume |
