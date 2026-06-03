import pandas as pd
from dataclasses import dataclass
from collections import defaultdict
from typing import Dict, List, Tuple

# ==========================================
# 1. 实体定义：清晰的数据结构 (DataClass)
# ==========================================
@dataclass
class Order:
    """定义一个存活在系统中的订单"""
    price: float
    volume: int
    side: str        # 'B' (买) 或 'S' (卖)
    order_type: str  # 'L' (限价单) 或 'M' (市价单)


# ==========================================
# 2. 核心状态机：高内聚，低耦合
# ==========================================
class ReadableOrderBook:
    def __init__(self):
        # 存活订单池：通过 OrderNo 快速查找订单详情
        self.active_orders: Dict[int, Order] = {}
        
        # 盘口价格树：自动处理默认值 0
        self.bids: Dict[float, int] = defaultdict(int)
        self.asks: Dict[float, int] = defaultdict(int)

    def process_event(self, event):
        """
        主路由：根据事件类型，分发给不同的处理函数。
        这里的 event 可以是 pd.itertuples() 返回的命名元组。
        """
        if event.EventType == 'O':
            self._handle_new_order(event)
        elif event.EventType == 'E':
            self._handle_execution(event)

    def _handle_new_order(self, event):
        """处理全新的挂单"""
        # 1. 实例化并注册订单
        self.active_orders[event.OrderNo] = Order(
            price=event.Price,
            volume=event.Volume,
            side=event.Side,
            order_type=event.OrderType
        )
        
        # 2. 只有限价单 (L) 才会在盘口上产生深度的累积
        if event.OrderType == 'L':
            if event.Side == 'B':
                self.bids[event.Price] += event.Volume
            elif event.Side == 'S':
                self.asks[event.Price] += event.Volume

    def _handle_execution(self, event):
        """处理成交或撤单 (对流动性的扣减)"""
        # 深交所的逻辑很简单：只要 Bid/Ask 编号存在且大于0，就去扣减对应的量
        if pd.notna(event.BidOrderNo) and event.BidOrderNo > 0:
            self._deduct_volume(int(event.BidOrderNo), event.Volume)
            
        if pd.notna(event.AskOrderNo) and event.AskOrderNo > 0:
            self._deduct_volume(int(event.AskOrderNo), event.Volume)

    def _deduct_volume(self, order_no: int, exec_volume: int):
        """核心流动性扣减逻辑，集中处理异常与状态清理"""
        # 防御性判断：如果该订单不存在（历史遗留或丢包），直接忽略
        if order_no not in self.active_orders:
            return
            
        order = self.active_orders[order_no]
        
        # 1. 扣减订单本身的可用数量
        order.volume -= exec_volume
        
        # 2. 如果是限价单，同步扣减对应价格档位的厚度
        if order.order_type == 'L':
            if order.side == 'B':
                self.bids[order.price] -= exec_volume
                # 当该档位数量归零(或异常小于0)，彻底清理该价位
                if self.bids[order.price] <= 0:
                    del self.bids[order.price]
                    
            elif order.side == 'S':
                self.asks[order.price] -= exec_volume
                # 同理，清理卖空档位
                if self.asks[order.price] <= 0:
                    del self.asks[order.price]
                    
        # 3. 订单生命周期管理：如果单子被吃光或撤完，从内存池中回收
        if order.volume <= 0:
            del self.active_orders[order_no]

    def get_snapshot(self, levels: int = 5) -> Dict[str, List[Tuple[float, int]]]:
        """按指定档位输出盘口快照"""
        # 买盘降序排 (买一是最贵的)，卖盘升序排 (卖一是最便宜的)
        sorted_bids = sorted(self.bids.items(), key=lambda x: x[0], reverse=True)
        sorted_asks = sorted(self.asks.items(), key=lambda x: x[0])
        
        return {
            'bids': sorted_bids[:levels],
            'asks': sorted_asks[:levels]
        }