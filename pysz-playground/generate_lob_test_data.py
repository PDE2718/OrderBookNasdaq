from __future__ import annotations

from pathlib import Path

import pandas as pd


DATA_DIR = Path(__file__).resolve().parent / "test_data"
DATA_FILE = DATA_DIR / "lob_events.csv"


def make_lob_test_dataframe() -> pd.DataFrame:
    """Build a fixed pandas event table for the Numba LOB tests."""

    df = pd.DataFrame(
        {
            "Seq": [1, 2, 3, 4, 5],
            "EventType": ["O", "O", "O", "E", "E"],
            "ExecType": [pd.NA, pd.NA, pd.NA, "T", "C"],
            "OrderNo": [1, 2, 3, pd.NA, pd.NA],
            "BidOrderNo": [pd.NA, pd.NA, pd.NA, 1, 2],
            "AskOrderNo": [pd.NA, pd.NA, pd.NA, 3, pd.NA],
            "Price": [10.10, 9.90, 10.10, 10.10, 9.90],
            "Volume": [1000, 500, 200, 200, 300],
            "Side": ["B", "B", "S", pd.NA, pd.NA],
            "OrderType": ["L", "L", "L", pd.NA, pd.NA],
        }
    )

    return df.astype(
        {
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
        }
    )


def main() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    df = make_lob_test_dataframe()
    df.to_csv(DATA_FILE, index=False)
    print(f"wrote {DATA_FILE}")
    print(df)


if __name__ == "__main__":
    main()
