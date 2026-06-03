# 假设 df 已经是按全局序号排好序的 Pandas DataFrame
# ==========================================
# 模拟一份小数据
data = {
    'EventType': ['O', 'O', 'O', 'E'],
    'OrderNo':   [1, 2, 3, None],
    'BidOrderNo':[None, None, None, 1],
    'AskOrderNo':[None, None, None, 3],
    'Price':     [10.1, 9.9, 10.1, 10.1],
    'Volume':    [1000, 500, 200, 200],
    'Side':      ['B', 'B', 'S', None],
    'OrderType': ['L', 'L', 'L', None],
}
df = pd.DataFrame(data)

# ==========================================
# 初始化与执行
lob = ReadableOrderBook()

# df.itertuples() 会将每一行变成一个可点读的属性 (如 row.Price)
for row in df.itertuples(index=False):
    lob.process_event(row)
    
    # 随时可以随地获取快照
    snap = lob.get_snapshot(levels=2)
    print(f"处理事件: {row.EventType}")
    print(f"买盘: {snap['bids']}")
    print(f"卖盘: {snap['asks']}")
    print("-" * 30)