# itch

`itch/` 是本项目的 C++20 核心子项目，负责：

```text
BinaryFILE 切帧 -> ITCH 消息解析 -> mini feed filter -> LOB 重构 -> HDF5 L2 快照导出
```

常用目标：

```text
itch                 核心库：parser、formatter、order_book
parse_messages       解析和打印消息
filter               按 symbol/locate 裁剪原始 ITCH 文件
export_l2_snapshots  重构 LOB 并导出 HDF5 L2/K 线快照
order_book_tests     LOB 单元测试
```

构建和完整运行流程见仓库根目录 [README.md](../README.md)。专题设计文档见：

- [消息对象模型](docs/messages_design.md)
- [ObjectPool 设计](docs/object_pool_design.md)
- [LOB 性能设计备忘录](docs/lob_performance_notes.md)
- [HDF5 L2 快照导出](docs/hdf5_snapshot_export.md)
- [中英术语对照表](../docs/glossary.md)
