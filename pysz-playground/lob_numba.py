from __future__ import annotations

import numpy as np
import pandas as pd
from numba import boolean, int8, int64, types, uint8
from numba.experimental import jitclass
from numba.typed import Dict


BUY = np.int8(1)
SELL = np.int8(-1)

LIMIT = np.uint8(1)
MARKET = np.uint8(2)

NO_ORDER = np.int64(0)


@jitclass(
    [
        ("price", int64),
        ("volume", int64),
        ("side", int8),
        ("order_type", uint8),
    ]
)
class Order:
    """One live order, kept as its own Numba object."""

    def __init__(self, price: int, volume: int, side: int, order_type: int):
        self.price = price
        self.volume = volume
        self.side = side
        self.order_type = order_type


ORDER_TYPE = Order.class_type.instance_type


@jitclass(
    [
        ("orders", types.DictType(int64, ORDER_TYPE)),
        ("bids", types.DictType(int64, int64)),
        ("asks", types.DictType(int64, int64)),
        ("strict", boolean),
        ("unknown_orders", int64),
        ("overfills", int64),
    ]
)
class NumbaOrderBook:
    """Readable Numba LOB with independent Order objects."""

    def __init__(self, strict: bool = True):
        self.orders = Dict.empty(key_type=int64, value_type=ORDER_TYPE)
        self.bids = Dict.empty(key_type=int64, value_type=int64)
        self.asks = Dict.empty(key_type=int64, value_type=int64)
        self.strict = strict
        self.unknown_orders = 0
        self.overfills = 0

    def add_order(
        self, order_no: int, price: int, volume: int, side: int, order_type: int
    ) -> None:
        self.orders[order_no] = Order(price, volume, side, order_type)

        if order_type == LIMIT:
            self._add_level(side, price, volume)

    def trade(self, bid_order_no: int, ask_order_no: int, volume: int) -> None:
        if bid_order_no != NO_ORDER:
            self._reduce_order(bid_order_no, volume)
        if ask_order_no != NO_ORDER:
            self._reduce_order(ask_order_no, volume)

    def cancel(self, bid_order_no: int, ask_order_no: int, volume: int) -> None:
        if bid_order_no != NO_ORDER:
            self._reduce_order(bid_order_no, volume)
        if ask_order_no != NO_ORDER:
            self._reduce_order(ask_order_no, volume)

    def best_bid(self) -> int:
        if len(self.bids) == 0:
            return 0

        best = -9223372036854775808
        for price in self.bids:
            if price > best:
                best = price
        return best

    def best_ask(self) -> int:
        if len(self.asks) == 0:
            return 0

        best = 9223372036854775807
        for price in self.asks:
            if price < best:
                best = price
        return best

    def price_volume(self, side: int, price: int) -> int:
        levels = self._levels(side)
        if price in levels:
            return levels[price]
        return 0

    def order_volume(self, order_no: int) -> int:
        if order_no in self.orders:
            return self.orders[order_no].volume
        return 0

    def _reduce_order(self, order_no: int, volume: int) -> None:
        if order_no not in self.orders:
            self.unknown_orders += 1
            if self.strict:
                raise KeyError("unknown order")
            return

        order = self.orders[order_no]
        if volume > order.volume:
            self.overfills += 1
            if self.strict:
                raise ValueError("order overfill")

        order.volume -= volume

        if order.order_type == LIMIT:
            self._add_level(order.side, order.price, -volume)

        if order.volume <= 0:
            del self.orders[order_no]

    def _add_level(self, side: int, price: int, volume_delta: int) -> None:
        levels = self._levels(side)

        if price in levels:
            next_volume = levels[price] + volume_delta
        else:
            next_volume = volume_delta

        if next_volume <= 0:
            if price in levels:
                del levels[price]
        else:
            levels[price] = next_volume

    def _levels(self, side: int):
        if side == BUY:
            return self.bids
        return self.asks


def process_message(
    book: NumbaOrderBook,
    message,
    *,
    price_scale: int = 10000,
) -> None:
    """Parse one pandas row message and feed it into the LOB.

    Intended usage:

        df = pd.read_csv(...)
        book = NumbaOrderBook()
        for row in df.itertuples(index=False):
            process_message(book, row)
    """

    event_type = _code(_field(message, "EventType"))

    if event_type in ("O", "ORDER", "ADD", "ADD_ORDER"):
        book.add_order(
            _required_int(_field(message, "OrderNo"), "OrderNo"),
            _price_tick(_field(message, "Price"), price_scale),
            _required_int(_field(message, "Volume"), "Volume"),
            _side_code(_field(message, "Side")),
            _order_type_code(_field(message, "OrderType")),
        )
        return

    bid_order_no = _optional_int(_field(message, "BidOrderNo", 0))
    ask_order_no = _optional_int(_field(message, "AskOrderNo", 0))
    volume = _required_int(_field(message, "Volume"), "Volume")

    if event_type in ("T", "TRADE"):
        book.trade(bid_order_no, ask_order_no, volume)
        return

    if event_type in ("C", "CANCEL"):
        book.cancel(bid_order_no, ask_order_no, volume)
        return

    if event_type in ("E", "EXEC", "EXECUTION"):
        exec_type = _code(_field(message, "ExecType", "T"))
        if exec_type in ("C", "CANCEL"):
            book.cancel(bid_order_no, ask_order_no, volume)
        elif exec_type in ("", "T", "TRADE", "F", "FILL"):
            book.trade(bid_order_no, ask_order_no, volume)
        else:
            raise ValueError(f"unknown ExecType: {exec_type!r}")
        return

    raise ValueError(f"unknown EventType: {event_type!r}")


def _field(message, name: str, default=None):
    if hasattr(message, name):
        return getattr(message, name)
    if isinstance(message, pd.Series):
        return message.get(name, default)
    if isinstance(message, dict):
        return message.get(name, default)
    return default


def _code(value) -> str:
    if _is_missing(value):
        return ""
    if isinstance(value, bytes):
        value = value.decode()
    return str(value).strip().upper()


def _side_code(value) -> int:
    code = _code(value)
    if code in ("B", "BUY"):
        return int(BUY)
    if code in ("S", "SELL"):
        return int(SELL)
    raise ValueError(f"unknown Side: {value!r}")


def _order_type_code(value) -> int:
    code = _code(value)
    if code in ("L", "LIMIT"):
        return int(LIMIT)
    if code in ("M", "MARKET"):
        return int(MARKET)
    raise ValueError(f"unknown OrderType: {value!r}")


def _price_tick(value, price_scale: int) -> int:
    if _is_missing(value):
        raise ValueError("Price is required")
    return int(round(float(value) * price_scale))


def _optional_int(value) -> int:
    if _is_missing(value):
        return int(NO_ORDER)
    return int(value)


def _required_int(value, field_name: str) -> int:
    if _is_missing(value):
        raise ValueError(f"{field_name} is required")
    return int(value)


def _is_missing(value) -> bool:
    if value is None:
        return True
    if isinstance(value, str) and value.strip() == "":
        return True
    return bool(pd.isna(value))
