# Tutorial

当前项目的完整教程已经合并到仓库根目录 [README.md](../../README.md)，包括：

```text
1. 项目导览和设计思路
2. 数据来源和本地目录布局
3. CMake/HighFive/HDF5 编译方式
4. filter 生成 mini feed
5. export_l2_snapshots 导出 60s HDF5
6. Python 批量画 L2/K 线图
```

如果想看更细的设计背景，请继续阅读：

- [消息对象模型](messages_design.md)
- [ObjectPool 设计](object_pool_design.md)
- [LOB 性能设计备忘录](lob_performance_notes.md)
- [HDF5 L2 快照导出](hdf5_snapshot_export.md)
