from pathlib import Path

import pandas as pd

from lob_simple import BUY, SELL, SimpleOrderBook, process_message


DATA_FILE = Path(__file__).resolve().parent / "test_data" / "lob_events.csv"


def replay_csv() -> SimpleOrderBook:
    df = pd.read_csv(DATA_FILE)
    book = SimpleOrderBook()

    for row in df.itertuples(index=False):
        process_message(book, row)

    return book


def test_simple_order_book():
    book = replay_csv()

    assert len(book.orders) == 2
    assert book.order_volume(1) == 800
    assert book.order_volume(2) == 200
    assert book.order_volume(3) == 0

    assert book.best_bid() == 101000
    assert book.best_ask() == 0
    assert book.price_volume(BUY, 101000) == 800
    assert book.price_volume(BUY, 99000) == 200
    assert book.price_volume(SELL, 101000) == 0


def main():
    test_simple_order_book()
    print(f"passed: {Path(__file__).name}")


if __name__ == "__main__":
    main()
