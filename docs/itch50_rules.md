# Nasdaq TotalView-ITCH 5.0 报文规则（中文解读）

本文基于你本地文件 `references/NQTVITCHSpecification.pdf`（36 页，创建时间 2024-02-28）整理，目标是服务于“订单簿重构 + 报文筛选/裁剪”实现。

## 1. 协议总览

- TotalView-ITCH 是逐笔、逐订单级别的市场数据流，覆盖：
  - 订单新增/修改/删除/成交
  - 系统与交易状态
  - 拍卖与不平衡信息
- ITCH 载荷通常通过更高层协议传输（SoupBinTCP / MoldUDP64）。
- `Stock Locate` 是当日动态分配的证券索引：
  - 当天内不变
  - 跨天不可复用
  - 出现在所有消息的相同位置（偏移 `1`，长度 `2`）
  - 非证券相关消息该字段为 `0`

## 2. 编码与字段基础规则

- 所有整数：`big-endian`（网络字节序），默认无符号。
- 字符字段：ASCII，左对齐，右侧空格填充。
- 价格字段：定点整数。
  - `Price(4)` 表示数值除以 `10^4`
  - `Price(8)` 表示数值除以 `10^8`
- 时间戳字段：`Timestamp` = 自当日 00:00:00 起的纳秒数（`6` 字节）。

## 3. 文件封装与消息边界（你这份历史文件的实践规则）

你当前样本 `data/raw/01302020.NASDAQ_ITCH50.bin` 的消息边界是：

- `2 字节消息体长度（big-endian） + ITCH 消息体`

例如开头：

- `00 0c`：后续消息体长度 12
- 消息体首字节 `53`（ASCII `S`）= System Event

说明：这个 2 字节长度前缀是历史文件封装常见形式；ITCH 规范正文主要描述“消息体字段”。

## 4. 消息类型一览（按功能分组）

下表长度是“消息体长度”（不含前置 2 字节长度）。

| 类型 | 名称 | 长度(字节) | 是否股票相关 | 关键说明 |
|---|---:|---:|---:|---|
| `S` | System Event | 12 | 否（Locate=0） | 日内阶段事件 |
| `R` | Stock Directory | 39 | 是 | 当日 `stock <-> locate` 映射及静态属性 |
| `H` | Stock Trading Action | 25 | 是 | 停牌/恢复/仅报价 |
| `Y` | Reg SHO Restriction | 20 | 是 | 卖空限制状态 |
| `L` | Market Participant Position | 26 | 是 | 做市商状态 |
| `V` | MWCB Decline Level | 35 | 否（Locate=0） | 大盘熔断阈值 |
| `W` | MWCB Status | 12 | 否（Locate=0） | 大盘熔断触发级别 |
| `K` | IPO Quoting Period Update | 28 | **按股票字段相关**（Locate=0） | 重点例外：有 `Stock` 但 Locate=0 |
| `J` | LULD Auction Collar | 35 | 是 | LULD 拍卖价带 |
| `h` | Operational Halt | 21 | 是 | 交易所级运营停复牌 |
| `A` | Add Order (No MPID) | 36 | 是 | 新增订单 |
| `F` | Add Order (With MPID) | 40 | 是 | 新增订单（带归属） |
| `E` | Order Executed | 31 | 是 | 挂单成交 |
| `C` | Order Executed w/ Price | 36 | 是 | 价格改进成交，含 Printable |
| `X` | Order Cancel | 23 | 是 | 部分撤单 |
| `D` | Order Delete | 19 | 是 | 全撤单 |
| `U` | Order Replace | 35 | 是 | 替换订单，生成新 Ref |
| `P` | Trade (Non-Cross) | 44 | 是 | 非展示订单成交，不改簿 |
| `Q` | Cross Trade | 40 | 是 | 开/收盘及特殊撮合成交汇总 |
| `B` | Broken Trade | 19 | 是 | 成交作废 |
| `I` | NOII | 50 | 是 | 开/收盘/复牌不平衡指标 |
| `N` | RPII | 20 | 是 | 零售价格改善指示 |
| `O` | DLCR Price Discovery | 48 | 是 | 仅直接上市募资品种 |

## 5. 核心业务语义（订单簿重构角度）

### 5.1 会话控制

`S` 消息事件码（`Event Code`）：

- `O`：Start of Messages（当日第一条业务消息）
- `S`：Start of System Hours
- `Q`：Start of Market Hours
- `M`：End of Market Hours
- `E`：End of System Hours
- `C`：End of Messages（当日最后一条）

要点：规范明确 `E` 后仍可能出现 `B` / `D` 等消息，直到 `C`。

### 5.2 证券主数据与状态

- `R`（Stock Directory）用于构建当日 `stock -> locate` 映射。
- `H` / `h` 分别反映跨市场交易状态与交易所运营状态。
- `Y`、`J`、`K`、`I`、`N`、`O` 为风控与拍卖/指示类补充信息。

### 5.3 订单生命周期（L3）

- 建簿入口：`A` / `F`
- 数量变化：`E` / `C` / `X`
- 移除：`D`
- 替换：`U`（旧 Ref 失效，新 Ref 生效）

### 5.4 成交与统计

- `E` / `C`：来自簿上订单的执行（影响该订单剩余量）
- `P`：非展示订单成交（通常不影响可见簿）
- `Q`：Cross 成交汇总
- `B`：成交作废

## 6. 与实现强相关的“易错点”

1. `Stock Locate` 跨日不稳定，不能缓存复用。  
2. `K` 消息是筛选逻辑中的关键例外：`Locate=0`，但有 `Stock`。  
3. `P` 消息的 `Order Reference Number` 规范说明可为 0；不要依赖它关联簿。  
4. `C` 消息有 `Printable` 字段：做成交统计时应按 `Y/N` 防止重复计量。  
5. `U` 不带 side/stock/mpid，需继承原订单属性。  
6. 股票代码是 8 字节、右侧空格填充，比较前应 `trim_right(' ')`。  
7. 解析器应对“长度不匹配/截断消息”做严格保护并记录偏移。

## 7. 针对“单股裁剪”的规则提炼

如果目标是“保留系统必要消息 + 指定股票（如 INTC）相关消息”，推荐：

- 必保留：`S`（会话边界）
- 按需求保留：`V/W`（全市场熔断信息，常用于完整语义）
- 指定股票：
  - `Locate` 命中目标 locate 集合的消息全部保留
  - 另外对 `K` 按 `Stock` 字段命中保留（因为 locate=0）
- `R` 建议至少保留目标股票对应记录（用于复核映射和元数据）

## 8. 建议的最小字段解析集合（为高性能过滤）

- 所有消息：`type@0`、`locate@1..2`
- `R`：`stock@11..18`（建映射）
- `K`：`stock@11..18`（例外筛选）

其余字段无需在裁剪阶段解码，直接原样透传即可。

---

参考：

- 本地规范文件：`references/NQTVITCHSpecification.pdf`
- 官方同名规范链接：<https://nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf>
