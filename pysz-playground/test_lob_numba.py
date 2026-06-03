from __future__ import annotations

from pathlib import Path

import pandas as pd

from generate_lob_test_data import DATA_FILE
from lob_numba import BUY, SELL, NumbaOrderBook, process_message


def load_test_dataframe() -> pd.DataFrame:
    if not DATA_FILE.exists():
        raise FileNotFoundError(
            f"{DATA_FILE} does not exist. Run generate_lob_test_data.py first."
        )

    return pd.read_csv(
        DATA_FILE,
        dtype={
            "Seq": "int64",
            "EventType": "string",
            "ExecType": "string",
            "OrderNo": "Int64",
            "BidOrderNo": "Int64",
            "AskOrderNo": "Int64",
            "Price": "float64",
            "Volume": "int64",
            "Side": "string",
            "OrderType": "string",
        },
    )


def replay_dataframe(df: pd.DataFrame) -> NumbaOrderBook:
    book = NumbaOrderBook(strict=True)
    for row in df.itertuples(index=False):
        process_message(book, row)
    return book


def test_numba_order_book_from_csv_messages() -> None:
    book = replay_dataframe(load_test_dataframe())

    assert len(book.orders) == 2
    assert book.order_volume(1) == 800
    assert book.order_volume(2) == 200
    assert book.order_volume(3) == 0

    assert book.best_bid() == 101000
    assert book.best_ask() == 0
    assert book.price_volume(BUY, 101000) == 800
    assert book.price_volume(BUY, 99000) == 200
    assert book.price_volume(SELL, 101000) == 0

    assert book.unknown_orders == 0
    assert book.overfills == 0


def main() -> None:
    test_numba_order_book_from_csv_messages()
    print(f"passed: {Path(__file__).name}")


if __name__ == "__main__":
    main()
