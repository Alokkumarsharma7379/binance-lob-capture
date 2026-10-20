"""Independent development-only validator; not part of the C++ application."""

import argparse
import collections
import csv
import json
from pathlib import Path
import re


def check(condition, message):
    if not condition:
        raise ValueError(message)


def read_log(path):
    raw = path.read_bytes()
    # Windows PowerShell 5 redirection uses UTF-16; the application CSV is UTF-8.
    return raw.decode("utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("--session-log", type=Path)
    parser.add_argument("--samples-log", type=Path)
    args = parser.parse_args()
    raw = args.csv_path.read_bytes()
    check(raw.endswith(b"\r\n"), "missing final CRLF")
    check(b"\r" not in raw.replace(b"\r\n", b"")
          and b"\n" not in raw.replace(b"\r\n", b""), "inconsistent row endings")
    header = "recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json"
    counts = collections.Counter()
    sequence = 0
    samples = {}
    if args.samples_log:
        lines = read_log(args.samples_log).splitlines()
        for index, line in enumerate(lines):
            match = re.fullmatch(r"Sample stream=(\S+) recv_tsec=(\d+) recv_tnsec=(\d+)", line)
            if match:
                outer = json.loads(lines[index + 1])
                samples[(match[2], match[3], match[1])] = outer["data"]
        check(len(samples) == 3, "expected three original session samples")
    stream_names = {"depth_diff": "btcusdt@depth@100ms", "depth5": "btcusdt@depth5@100ms",
                    "trade": "btcusdt@trade"}
    with args.csv_path.open(encoding="utf-8", newline="") as source:
        reader = csv.reader(source, strict=True)
        check(next(reader) == header.split(","), "header mismatch")
        for number, row in enumerate(reader, start=2):
            check(len(row) == 9, f"row {number}: expected nine logical fields")
            for index in (0, 1, 4, 5, 6):
                check(re.fullmatch(r"-?\d+", row[index]) is not None, f"row {number}: noninteger")
            check(-(2**63) <= int(row[0]) < 2**63 and 0 <= int(row[1]) < 10**9,
                  f"row {number}: timestamp range")
            check(row[2] == "spot" and row[3] in stream_names and row[4:6] == ["0", "0"]
                  and row[7] == "BTCUSDT", f"row {number}: metadata")
            current = int(row[6])
            check(sequence < current < 2**64, f"row {number}: sequence not increasing")
            sequence = current
            payload = json.loads(row[8])
            check(isinstance(payload, dict) and "stream" not in payload and "data" not in payload,
                  f"row {number}: expected inner object")
            check(json.dumps(payload, separators=(",", ":"), ensure_ascii=False) == row[8],
                  f"row {number}: not minified")
            if "s" in payload:
                check(payload["s"] == row[7], f"row {number}: symbol mismatch")
            if row[3] in ("depth_diff", "trade"):
                check(payload.get("e") == ("depthUpdate" if row[3] == "depth_diff" else "trade"),
                      f"row {number}: event classification")
            else:
                check(all(key in payload for key in ("lastUpdateId", "bids", "asks")),
                      f"row {number}: partial snapshot fields")
            key = (row[0], row[1], stream_names[row[3]])
            if key in samples:
                check(payload == samples.pop(key), f"row {number}: original sample changed")
            counts[row[3]] += 1
    check(all(counts[kind] > 0 for kind in stream_names), "not all streams represented")
    check(not samples, "session samples missing from CSV")
    total = sum(counts.values())
    if args.session_log:
        log = read_log(args.session_log)
        matches = re.findall(r"Messages=(\d+) depth_diff=(\d+) depth5=(\d+) trade=(\d+) market_rows=(\d+)", log)
        check(bool(matches), "no session counters")
        values = tuple(map(int, matches[-1]))
        check(values == (total, counts["depth_diff"], counts["depth5"], counts["trade"], total),
              "CSV rows differ from session counters")
        check("Parse errors=0 connection errors=0 I/O errors=0" in log, "session error counters")
    print(json.dumps({"file": str(args.csv_path.resolve()), "bytes": len(raw), "rows": total,
                      "counts": dict(counts), "last_conn_seq": sequence,
                      "validation": "passed"}, indent=2))
    print("Exact header and first three real data rows:")
    for line in raw.decode("utf-8").split("\r\n")[:4]:
        print(line)


if __name__ == "__main__":
    main()
