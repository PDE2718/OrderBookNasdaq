#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

import h5py
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np


PLAYGROUND_ROOT = Path(__file__).resolve().parents[1]


def attr_text(value) -> str:
    if isinstance(value, np.ndarray):
        value = value.item()
    if isinstance(value, bytes):
        return value.decode("utf-8")
    return str(value)


def attr_int(value) -> int:
    if isinstance(value, np.ndarray):
        return int(value.item())
    return int(value)


def load_snapshot(path: Path) -> dict[str, np.ndarray | dict]:
    with h5py.File(path, "r") as file:
        attrs = {key: file.attrs[key] for key in file.attrs.keys()}
        arrays = {
            "meta": attrs,
            "start_ns": file["bar/start_ns"][:],
            "end_ns": file["bar/end_ns"][:],
            "open_px": file["bar/open_px"][:],
            "high_px": file["bar/high_px"][:],
            "low_px": file["bar/low_px"][:],
            "close_px": file["bar/close_px"][:],
            "volume": file["bar/volume"][:],
            "has_trade": file["bar/has_trade"][:].astype(bool),
            "best_bid": file["book/best_bid"][:],
            "best_ask": file["book/best_ask"][:],
            "bid_shares": file["book/bid_shares"][:],
            "ask_shares": file["book/ask_shares"][:],
        }
    return arrays


def price(raw: np.ndarray, scale: int) -> np.ndarray:
    out = raw.astype(np.float64) / scale
    out[raw == 0] = np.nan
    return out


def session_times(end_ns: np.ndarray) -> np.ndarray:
    base = np.datetime64("1970-01-01T00:00:00")
    timestamps = base + end_ns.astype("timedelta64[ns]")
    return mdates.date2num(timestamps)


def interval_label(meta: dict) -> str:
    interval_ns = attr_int(meta["interval_ns"])
    if interval_ns % 1_000_000_000 == 0:
        return f"{interval_ns // 1_000_000_000}s"
    if interval_ns % 1_000_000 == 0:
        return f"{interval_ns // 1_000_000}ms"
    return f"{interval_ns}ns"


def plot(snapshot_path: Path, output: Path) -> None:
    data = load_snapshot(snapshot_path)
    meta = data["meta"]
    symbol = attr_text(meta["symbol"])
    scale = attr_int(meta["price_scale"])
    depth = attr_int(meta["depth"])
    times = session_times(data["end_ns"])

    close_px = price(data["close_px"], scale)
    high_px = price(data["high_px"], scale)
    low_px = price(data["low_px"], scale)
    best_bid = price(data["best_bid"], scale)
    best_ask = price(data["best_ask"], scale)
    volume = data["volume"]
    has_trade = data["has_trade"]

    bid_liquidity = np.log10(data["bid_shares"].astype(np.float64).T + 1.0)
    ask_liquidity = np.log10(data["ask_shares"].astype(np.float64).T + 1.0)

    fig = plt.figure(figsize=(16, 10), constrained_layout=True)
    grid = fig.add_gridspec(4, 1, height_ratios=[2.2, 0.7, 1.25, 1.25])
    ax_price = fig.add_subplot(grid[0])
    ax_volume = fig.add_subplot(grid[1], sharex=ax_price)
    ax_bid = fig.add_subplot(grid[2], sharex=ax_price)
    ax_ask = fig.add_subplot(grid[3], sharex=ax_price)

    ax_price.plot(times, best_bid, linewidth=0.85, color="#166534", label="best bid")
    ax_price.plot(times, best_ask, linewidth=0.85, color="#991b1b", label="best ask")
    ax_price.plot(times, close_px, linewidth=1.1, color="#111827", label="close")
    ax_price.fill_between(times, low_px, high_px, where=has_trade, color="#94a3b8", alpha=0.25)
    ax_price.set_title(f"{symbol} visible trades and L2 snapshots ({interval_label(meta)})")
    ax_price.set_ylabel("price")
    ax_price.grid(True, alpha=0.25)
    ax_price.legend(loc="upper left", ncols=3)

    bar_width = (times[1] - times[0]) * 0.8 if len(times) > 1 else 1 / (24 * 60)
    ax_volume.bar(times, volume, width=bar_width, color="#64748b")
    ax_volume.set_ylabel("shares")
    ax_volume.grid(True, axis="y", alpha=0.25)

    extent = [times[0], times[-1], depth + 0.5, 0.5]
    bid_img = ax_bid.imshow(
        bid_liquidity,
        aspect="auto",
        interpolation="nearest",
        extent=extent,
        cmap="Greens",
    )
    ask_img = ax_ask.imshow(
        ask_liquidity,
        aspect="auto",
        interpolation="nearest",
        extent=extent,
        cmap="Reds",
    )
    ax_bid.set_ylabel("bid level")
    ax_ask.set_ylabel("ask level")
    ax_ask.set_xlabel("time")
    ax_bid.set_yticks(np.arange(1, depth + 1))
    ax_ask.set_yticks(np.arange(1, depth + 1))

    fig.colorbar(bid_img, ax=ax_bid, label="log10(shares + 1)")
    fig.colorbar(ask_img, ax=ax_ask, label="log10(shares + 1)")

    locator = mdates.MinuteLocator(byminute=range(0, 60, 30))
    formatter = mdates.DateFormatter("%H:%M")
    ax_ask.xaxis.set_major_locator(locator)
    ax_ask.xaxis.set_major_formatter(formatter)

    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=160)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        type=Path,
        default=None,
        help="HDF5 snapshot file produced by export_l2_snapshots.",
    )
    parser.add_argument(
        "--input-root",
        type=Path,
        default=PLAYGROUND_ROOT / "data/l2_hdf5_60s",
        help="Directory containing SYMBOL/snapshot.h5 files.",
    )
    parser.add_argument(
        "--symbols",
        default="",
        help="Comma-separated symbols to plot. Defaults to every directory under input-root.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output PNG path.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=PLAYGROUND_ROOT / "figures",
        help="Directory for batch output.",
    )
    args = parser.parse_args()

    if args.input is not None:
        output = args.output
        if output is None:
            output = args.output_dir / f"{args.input.parent.name}_l2.png"
        plot(args.input, output)
        print(f"wrote {output}")
        return

    if args.symbols:
        symbols = [item.strip().upper() for item in args.symbols.split(",") if item.strip()]
    else:
        symbols = sorted(path.name for path in args.input_root.iterdir() if path.is_dir())

    for symbol in symbols:
        snapshot = args.input_root / symbol / "snapshot.h5"
        output = args.output_dir / f"{symbol}_l2_60s.png"
        plot(snapshot, output)
        print(f"wrote {output}")


if __name__ == "__main__":
    main()
