"""Development evidence checker. No application replay mode or generated book output."""
import argparse
import collections
import csv
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re

from validate_market_csv import check, read_log

BOOK_HEADER = (
    "tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,"
    "bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,ask0,ask1,ask2,ask3,ask4,"
    "ask_size0,ask_size1,ask_size2,ask_size3,ask_size4"
)


def scaled(text):
    value = Decimal(text) * 100000000
    check(value == value.to_integral_value() and 0 <= value < 2**63, "invalid scaled value")
    return int(value)


def snapshot_fields(payload):
    bids = [(scaled(p), scaled(q)) for p, q in payload["bids"]]
    asks = [(scaled(p), scaled(q)) for p, q in payload["asks"]]
    bids += [(0, 0)] * (5 - len(bids))
    asks += [(0, 0)] * (5 - len(asks))
    return [p for p, q in bids] + [q for p, q in bids] + [p for p, q in asks] + [q for p, q in asks]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("market", type=Path)
    parser.add_argument("book", type=Path)
    parser.add_argument("--session-log", type=Path, required=True)
    args = parser.parse_args()
    with args.market.open(encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source, strict=True))
    counts = collections.Counter(r["stream_kind"] for r in rows)
    triggers = collections.defaultdict(list)
    snapshots = {}
    for index, row in enumerate(rows):
        if row["stream_kind"] == "trade":
            continue
        kind = "P" if row["stream_kind"] == "depth5" else "D"
        payload = json.loads(row["payload_json"])
        triggers[(row["recv_tsec"], row["recv_tnsec"], kind)].append((index, payload))
        if kind == "P":
            snapshots[payload["lastUpdateId"]] = payload
    raw = args.book.read_bytes()
    check(raw.endswith(b"\r\n") and b"\n" not in raw.replace(b"\r\n", b""), "book CRLF")
    book_counts = collections.Counter()
    previous_trigger = -1
    count = 0
    matched_diff_snapshots = 0
    with args.book.open(encoding="utf-8", newline="") as source:
        reader = csv.reader(source, strict=True)
        check(next(reader) == BOOK_HEADER.split(","), "book header mismatch")
        for count, row in enumerate(reader, 1):
            check(len(row) == 26, f"book row {count}: column count")
            for field in row[:4] + row[6:]:
                check(re.fullmatch(r"\d+", field) is not None, f"book row {count}: nonnegative integer")
            check(int(row[1]) < 10**9 and int(row[2]) == count and row[3] == "1", "time/sequence/id")
            check(row[4] in ("P", "D") and row[5] in ("B", "S", "N"), "type/side")
            values = list(map(int, row[6:]))
            check(all(x < 2**63 for x in values), "book int64 range")
            bids, bq, asks, aq = values[:5], values[5:10], values[10:15], values[15:]
            for prices, sizes, reverse in ((bids, bq, True), (asks, aq, False)):
                positive = [p for p in prices if p]
                check(positive == sorted(set(positive), reverse=reverse), "strict book ordering")
                check(prices == positive + [0] * (5-len(positive)), "padding must be trailing")
                check(all((p == 0 and q == 0) or (p > 0 and q > 0) for p, q in zip(prices, sizes)), "price/size pairs")
            check(not bids[0] or not asks[0] or bids[0] < asks[0], "crossed book")
            candidates = triggers[(row[0], row[1], row[4])]
            candidates = [(i, p) for i, p in candidates if i > previous_trigger]
            check(len(candidates) == 1, "missing or ambiguous original triggering event")
            previous_trigger, payload = candidates[0]
            if row[4] == "P":
                check(row[5] == "N" and values == snapshot_fields(payload), "snapshot row differs from payload")
            else:
                expected_side = "N" if bool(payload["b"]) == bool(payload["a"]) else ("B" if payload["b"] else "S")
                check(row[5] == expected_side, "diff side")
                check(all(bids) and all(asks), "diff must not pad unknown depth")
                if payload["u"] in snapshots:
                    check(values == snapshot_fields(snapshots[payload["u"]]), "diff disagrees with same-ID exchange snapshot")
                    matched_diff_snapshots += 1
            book_counts[row[4]] += 1
    check(count > 0, "empty book CSV")
    log = read_log(args.session_log)
    summary = re.findall(r"Book rows=.*", log)
    check(len(summary) == 1, "missing book summary")
    totals = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", summary[0])}
    check(totals["rows"] == count and totals["applied_snapshot"] == book_counts["P"]
          and totals["applied_diff"] == book_counts["D"], "applied counters mismatch")
    check(totals["book_errors"] == 0, "book errors reported")
    decisions = ("applied_snapshot", "applied_diff", "stale_snapshot", "stale_diff", "waiting", "gap", "coverage_lost", "crossed")
    check(sum(totals[key] for key in decisions) == counts["depth5"] + counts["depth_diff"], "depth decision accounting")
    print(json.dumps({"validation": "passed", "market_rows": len(rows), "market_counts": dict(counts),
                      "book_rows": count, "book_types": dict(book_counts), "decisions": totals,
                      "diff_rows_compared_to_exchange_snapshot": matched_diff_snapshots,
                      "files": [{"path": str(p.resolve()), "bytes": p.stat().st_size,
                                 "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in (args.market, args.book)]}, indent=2))


if __name__ == "__main__":
    main()
