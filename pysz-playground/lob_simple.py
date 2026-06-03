from collections import defaultdict
from dataclasses import dataclass

import pandas as pd


BUY = 1
SELL = -1

LIMIT = 1
MARKET = 2

NO_ORDER = 0
PRICE_SCALE = 10000


@dataclass
class Order:
    """A live order inside the reconstructed order book."""

    price: int
    volume: int
    side: int
    order_type: int


class SimpleOrderBook:
    """Small, readable LOB state machine for explaining the core idea."""

    def __init__(self):
        self.orders = {}
        self.bids = defaultdict(int)
        self.asks = defaultdict(int)

    def add_order(self, order_no: int, order: Order):
        self.orders[order_no] = order

        if order.order_type == LIMIT:
            self._levels(order.side)[order.price] += order.volume

    def trade(self, bid_order_no: int, ask_order_no: int, volume: int):
        self._reduce_order(bid_order_no, volume)
        self._reduce_order(ask_order_no, volume)

    def cancel(self, bid_order_no: int, ask_order_no: int, volume: int):
        self._reduce_order(bid_order_no, volume)
        self._reduce_order(ask_order_no, volume)

    def best_bid(self) -> int:
        return max(self.bids, default=0)

    def best_ask(self) -> int:
        return min(self.asks, default=0)

    def price_volume(self, side: int, price: int) -> int:
        return self._levels(side).get(price, 0)

    def order_volume(self, order_no: int) -> int:
        order = self.orders.get(order_no)
        return 0 if order is None else order.volume

    def _reduce_order(self, order_no: int, volume: int):
        if order_no == NO_ORDER:
            return

        order = self.orders.get(order_no)
        if order is None:
            raise KeyError(f"unknown order: {order_no}")
        if volume > order.volume:
            raise ValueError(f"overfill order {order_no}: {volume} > {order.volume}")

        order.volume -= volume

        if order.order_type == LIMIT:
            levels = self._levels(order.side)
            levels[order.price] -= volume
            if levels[order.price] <= 0:
                del levels[order.price]

        if order.volume == 0:
            del self.orders[order_no]

    def _levels(self, side: int):
        return self.bids if side == BUY else self.asks


def process_message(book: SimpleOrderBook, message):
    """Turn one pandas row message into one LOB state transition."""

    event_type = code(message.EventType)

    if event_type == "O":
        order = Order(
            price=price_tick(message.Price),
            volume=int(message.Volume),
            side=side_code(message.Side),
            order_type=order_type_code(message.OrderType),
        )
        book.add_order(int(message.OrderNo), order)
        return

    if event_type == "E":
        bid_order_no = order_no_or_zero(message.BidOrderNo)
        ask_order_no = order_no_or_zero(message.AskOrderNo)
        volume = int(message.Volume)

        if code(message.ExecType) == "C":
            book.cancel(bid_order_no, ask_order_no, volume)
        else:
            book.trade(bid_order_no, ask_order_no, volume)
        return

    raise ValueError(f"unknown EventType: {message.EventType!r}")


def price_tick(price) -> int:
    return int(round(float(price) * PRICE_SCALE))


def order_no_or_zero(value) -> int:
    return NO_ORDER if pd.isna(value) else int(value)


def side_code(value) -> int:
    if code(value) == "B":
        return BUY
    if code(value) == "S":
        return SELL
    raise ValueError(f"unknown Side: {value!r}")


def order_type_code(value) -> int:
    if code(value) == "L":
        return LIMIT
    if code(value) == "M":
        return MARKET
    raise ValueError(f"unknown OrderType: {value!r}")


def code(value) -> str:
    if pd.isna(value):
        return ""
    return str(value).strip().upper()
