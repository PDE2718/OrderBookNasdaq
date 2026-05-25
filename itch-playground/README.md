# itch-playground

`itch-playground/` 保存 Python 研究脚本和本地生成图表。生成的 HDF5 和 PNG 默认被 `.gitignore` 排除。

批量画图：

```bash
cd itch-playground
source "$(conda info --base)/etc/profile.d/conda.sh"
conda activate MLbase

python scripts/plot_orderbook_l2.py \
  --input-root data/l2_hdf5_60s \
  --output-dir figures
```

单只股票：

```bash
python scripts/plot_orderbook_l2.py \
  --input data/l2_hdf5_60s/AAPL/snapshot.h5 \
  --output figures/AAPL_l2_60s.png
```

完整数据准备、C++ 导出和项目说明见根目录 [README.md](../README.md)。
