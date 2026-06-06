# 高性能 `float64 -> int64` 有序 Map 设计笔记

这份笔记记录刚才讨论的单模块数据结构需求：

- key 是 `float64`
- value 是 `int64`
- 行为上像 Python `dict`
- 额外支持按 key 正序或逆序取前若干个 item
- 如果数量不足，用默认 key/value 填充
- 最终目标是给 LOB 价位结构使用

## 一、需求本质

普通 Python `dict` 或 Numba `typed.Dict` 都是 hash map。它们适合：

- `set`
- `get`
- `del`
- `contains`

但它们不维护 key 的顺序。如果要频繁取：

- 最小的 N 个 item
- 最大的 N 个 item

就需要一个 ordered map，而不是 hash map。

对 LOB 来说：

- 卖盘 `asks` 常用正序前 N 个价格档。
- 买盘 `bids` 常用逆序前 N 个价格档。

所以这个结构更像一棵价格树。

## 二、可选实现路线

### 1. Python 原型：`sortedcontainers.SortedDict`

优点：

- 写起来最快。
- API 友好。
- 适合验证逻辑。

缺点：

- 不是 C/C++ 级别性能。
- 高频更新和取档时会有 Python 对象开销。

### 2. Numba

Numba 的 `typed.Dict` 是 typed hash dict，不是 ordered map。

如果用 Numba 做这个结构，需要额外维护：

- 排序数组
- 堆
- 自己写树

这会让代码复杂度上升很多，而且不一定比 C++ 清爽。

结论：Numba 适合 LOB 撮合状态机，但不太适合直接实现一个通用的高性能 ordered map。

### 3. C++ + pybind11

这是比较合理的生产路线。

C++ 内部可以先用：

```cpp
std::map<double, int64_t>
```

复杂度：

| 操作 | 复杂度 |
| --- | --- |
| set | `O(log n)` |
| get | `O(log n)` |
| erase | `O(log n)` |
| first(k) | `O(k)` |
| last(k) | `O(k)` |

第一版用 `std::map` 的好处是：

- 依赖少。
- 行为清楚。
- 有序遍历天然支持。
- 很容易用 pybind11 包成 Python 类。

以后如果发现价位数量有限且更新极多，可以再考虑：

- `boost::container::flat_map`
- `absl::btree_map`
- 自己实现跳表或 B-tree

## 三、推荐 Python API

建议接口像一个轻量 dict：

```python
m = FloatIntMap()

m[10.10] = 1000
m[9.90] = 500

assert m[10.10] == 1000
assert 10.10 in m
assert len(m) == 2

keys, values = m.first(5, key_default=float("nan"), value_default=0)
keys, values = m.last(5, key_default=float("nan"), value_default=0)
```

语义：

- `first(k)`：按 key 从小到大取 k 个。
- `last(k)`：按 key 从大到小取 k 个。
- 不足 k 个的位置用默认值填充。
- 返回两个 NumPy array：`keys: float64`，`values: int64`。

用于 LOB：

```python
ask_prices, ask_volumes = asks.first(10, float("nan"), 0)
bid_prices, bid_volumes = bids.last(10, float("nan"), 0)
```

## 四、C++ 类的大致形状

```cpp
class FloatIntMap {
public:
    void set(double key, int64_t value);
    int64_t get(double key, int64_t default_value) const;
    bool contains(double key) const;
    bool erase(double key);
    void clear();
    size_t size() const;

    py::tuple first(size_t n, double key_default, int64_t value_default) const;
    py::tuple last(size_t n, double key_default, int64_t value_default) const;

private:
    std::map<double, int64_t> data_;
};
```

`first()` 和 `last()` 不建议返回 `list[tuple[float, int]]`，因为那会创建很多 Python 对象。

更推荐直接返回：

```python
(keys_array, values_array)
```

也就是：

```cpp
return py::make_tuple(keys, values);
```

## 五、`py::tuple` 和变长返回

`py::tuple` 是 Python 对象，长度可以在 C++ 运行时决定。

例如：

```cpp
py::tuple result(2);
result[0] = keys_array;
result[1] = values_array;
return result;
```

这里 tuple 的长度固定是 2，但里面的 NumPy array 长度可以由运行时的 `n` 决定：

```cpp
py::array_t<double> keys(n);
py::array_t<int64_t> values(n);
```

Python 侧：

```python
keys, values = m.first(10)
```

其中：

```python
len(keys) == 10
len(values) == 10
```

也可以返回真正变长的 tuple：

```cpp
py::tuple result(n);
for (size_t i = 0; i < n; ++i) {
    result[i] = py::make_tuple(key, value);
}
```

但这个方案不适合高性能路径，因为每个 item 都会创建 Python tuple。

结论：

> 外层返回固定长度 `py::tuple(keys, values)`，变长发生在 NumPy array 内部。

## 六、`first()` / `last()` 的填充逻辑

正序取前 N 个：

```cpp
py::array_t<double> keys(n);
py::array_t<int64_t> values(n);

auto k = keys.mutable_unchecked<1>();
auto v = values.mutable_unchecked<1>();

for (size_t i = 0; i < n; ++i) {
    k(i) = key_default;
    v(i) = value_default;
}

size_t i = 0;
for (auto it = data_.begin(); it != data_.end() && i < n; ++it, ++i) {
    k(i) = it->first;
    v(i) = it->second;
}

return py::make_tuple(keys, values);
```

逆序取前 N 个：

```cpp
size_t i = 0;
for (auto it = data_.rbegin(); it != data_.rend() && i < n; ++it, ++i) {
    k(i) = it->first;
    v(i) = it->second;
}
```

这种实现的好处：

- 调用一次返回 N 档。
- 不在 C++/Python 之间反复跨边界。
- 不创建大量 Python tuple。
- 对 LOB 快照很自然。

## 七、浮点 key 的注意事项

如果 key 是 `double`，需要特别处理这些情况：

### 1. 拒绝 NaN

`NaN` 会破坏有序 map 的比较语义。插入、查询、删除时都应该检查：

```cpp
if (std::isnan(key)) {
    throw std::invalid_argument("NaN key is not allowed");
}
```

### 2. 统一 `-0.0` 和 `0.0`

可以在入口归一化：

```cpp
if (key == 0.0) {
    key = 0.0;
}
```

### 3. 价格最好还是用整数 tick

如果这个结构最终服务于价格档，最稳的 key 其实是 `int64 price_tick`，例如：

```python
price_tick = round(price * 10000)
```

但如果业务明确要求 `float64` key，那就把它当作二进制 double key 使用，不要期待十进制价格语义完全自然。

## 八、后续实现建议

第一版可以做一个很小的 pybind11 模块：

```text
float_int_map/
  pyproject.toml
  src/
    float_int_map.cpp
  tests/
    test_float_int_map.py
```

优先实现：

- `__len__`
- `__contains__`
- `__getitem__`
- `__setitem__`
- `__delitem__`
- `get`
- `clear`
- `first`
- `last`

测试重点：

- 空 map 取 N 档时填默认值。
- 数量不足时补默认值。
- 正序和逆序顺序正确。
- 删除 key 后档位消失。
- 更新已有 key 时 value 被覆盖。
- `NaN` key 抛错。
- `0.0` 和 `-0.0` 行为一致。

参考：

- pybind11 文档：https://pybind11.readthedocs.io/en/stable/
- pybind11 STL 支持：https://pybind11.readthedocs.io/en/latest/advanced/cast/stl.html
- Numba typed.Dict 文档：https://numba.readthedocs.io/en/stable/reference/pysupported.html
- SortedContainers SortedDict：https://grantjenks.com/docs/sortedcontainers/sorteddict.html
