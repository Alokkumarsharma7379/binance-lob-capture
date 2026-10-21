# Stage 4E fixture

`stage4e_market.csv` contains 27 hand-constructed events. `stage4e_expected_orderbook.csv` contains 15 independently calculated expected rows: nine snapshots and six diffs. Prices and sizes are multiplied by 100000000. Both files deliberately use CRLF.

Initial bids are `100:10, 99:9, 98:8, 97:7, 96:6`; asks are `102:12, 103:13, 104:14, 105:15, 106:16`.

| Input event | Expected effect |
| --- | --- |
| 1 | Diff before any snapshot: wait. |
| 2 | Snapshot 100: output row 1. |
| 3 | Trade with comma, quote, escaped newline and UTF-8: audit only; connection sequence skips 3. |
| 4 | Overlapping diff ending 101: replace bid 100 quantity with 5; output row 2 with an earlier wall-clock second. |
| 5 | Diff 102: insert bid 101:2 and replace ask 102 quantity with 3; row 3. |
| 6 | Diff 103: delete bid 101 and nonexistent 999; promote known bid 96; row 4. |
| 7–8 | Duplicate diff and stale snapshot: no rows. |
| 9–11 | Gap to 105, late diff 104 and stale snapshot 104: no rows; retain high watermark 105. |
| 12 | Snapshot 105 restores the original levels; row 5. |
| 13 | Delete fifth bid 96 without a known sixth: coverage loss, no row. |
| 14–15 | Snapshot 106 supplies bid 95:5 as the new fifth; matching repeat also emits: rows 6–7. |
| 16–17 | Trade begins epoch 1 and invalidates state; diff before a new anchor waits. |
| 18–19 | Short snapshot 2 has two levels per side; matching repeat emits again: rows 8–9, trailing zero pairs, still not diff-ready. |
| 20 | Diff 3 waits after short snapshots. |
| 21 | Full snapshot 4 restores original levels: row 10. |
| 22 | Ask-only diff 5 replaces quantity at 103 with 20: row 11, side S. |
| 23 | Bid-only diff 6 inserts 101:1: row 12; previous fifth is retained internally. |
| 24 | Matching snapshot 6 replaces the view: row 13. |
| 25 | Empty diff 7 still advances a valid revision: unchanged row 14, side N. |
| 26 | Diff 8 inserts bid 102, crossing ask 102: invalidate, no row. |
| 27 | Snapshot 8 restores original levels: row 15. |

Expected decisions: applied snapshots 9, applied diffs 6, stale snapshots 2, stale diffs 1, waiting 4, gap 1, coverage loss 1, crossed 1. Two trades generate no book decisions. Book sequence continues across the epoch change.
