# Stage 4D validation — October 21, 2026

Stage 4D is implemented and tested on the existing Windows x64 portable toolchain. The live capture and both file validators passed, with a material limitation: the live sample emitted only `P` rows because every diff arrived after an equal-revision depth5 snapshot. Differential application has deterministic offline coverage, but was not exercised as live `D` output in this run. This is not unconditional assignment acceptance or full-depth synchronization.

Starting and ending Git HEAD: `eca42cbcdf2a4c383e615f819f75962263d8d3f9`. The working tree was clean before Stage 4D. Nothing was committed or pushed. No tracked files were deleted. The PDF and `docs/requirements.md` remain byte-for-byte equal to HEAD. Requirements SHA-256: `3428ef37f2a671831a5f95661038e04a81733368a7c1502f5d64dfe1d1ddcb83`.

## Implementation and scope

| Files | Change |
| --- | --- |
| `src/decimal.hpp/.cpp` | Checked decimal-to-int64 conversion at scale 100000000, without floating point or rounding. |
| `src/depth_event.hpp/.cpp` | Decode Spot depth fields from the already parsed JSON DOM. |
| `src/order_book.hpp/.cpp` | Plain events, descending bid map, ascending ask map, coverage bounds, revision policy and decisions. No files, clocks or networking. |
| `src/csv_file.hpp/.cpp` | Extract existing exclusive file creation and checked buffered I/O for reuse by both writers. |
| `src/order_book_csv.hpp/.cpp` | Exact 26-column formatting, `_orderbook.csv` suffix, independent emitted-row sequence. |
| `src/event_processing.hpp/.cpp` | Write audit first, observe epoch, decode/apply depth, write committed book row, count the outcome. Trades are audit-only. |
| `src/envelope.hpp/.cpp` | Retain the owned inner JSON object alongside its minified audit representation; no second JSON parse. |
| `src/market_data.hpp/.cpp` | Use shared file ownership; expose epoch in metadata, defaulting to zero. The nine-column schema is unchanged. |
| `src/session.cpp`, `src/config.cpp` | Integrate the processing path, both file lifecycles, recovery/error counters and current help text. Existing network lifecycle remains intact. |
| `tests/decimal_tests.cpp`, `tests/order_book_tests.cpp` | Offline numeric, sequencing, coverage, formatting and pipeline checks. |
| `tests/validate_capture_pair.py` | Independent Python CSV evidence checker, not an application replay mode. |
| `CMakeLists.txt` | Add application sources and two CTest executables. Dependencies still discovered locally. |
| `.vscode/c_cpp_properties.json` | Point IntelliSense at the actual Stage 4D compilation database. No claim that VS Code UI diagnostics were inspected in this stage. |
| `.gitignore` | Ignore Python's generated `__pycache__` from development validation. |
| `README.md`, `docs/design.md`, this file | Current behavior, implementation decisions, evidence and limits. |

No REST client, replay, USD-M, automatic reconnect, threading changes, benchmarking or dependency downloads were added.

## Algorithm and assumptions

An accepted depth5 replaces both maps. Its fifth bid and ask establish complete known price ranges, conditional on the snapshot and diff IDs describing the same symbol revision. Retain all positive levels inside those ranges, including levels temporarily below the displayed fifth. A diff sets absolute quantities; zero removes a level. Trades do not subtract anything.

Let `L` be the last committed revision and `H` the highest valid depth ID observed in the epoch. `H` also covers all published revisions and survives invalidation:

- Snapshot `S < H`: audit and count stale; no mutation/output. Otherwise replace and emit `P`. If ready and `S == L`, require exact top-five agreement first.
- Not ready: audit/count the diff as waiting, update `H`, emit no book row.
- Ready and `u <= L`: covered/stale. At `u == L`, compare in-range changed quantities to current state when present; contradictions are fatal.
- `u > L`, `U > L+1`: gap, clear maps, wait for a snapshot at least as fresh as `H`.
- `U <= L+1 <= u`: apply validated absolute updates inside coverage. Commit and emit `D` only with at least five known levels on each side and an uncrossed book. Otherwise clear and count coverage loss/crossing.
- A short snapshot emits exactly its payload with zero padding, but does not enable diff reconstruction. Epoch changes clear state and watermarks; book CSV `seqNo` continues. Epoch reset is tested directly; live reconnect remains unimplemented.

The official [Spot stream documentation](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md) defines partial depth `lastUpdateId`, diff `U/u`, absolute quantities and zero deletion. Its complete bootstrap procedure uses a REST snapshot with buffered diffs. It does not promise globally ordered arrivals across these subscriptions. Our cross-stream anchoring and coverage/drop policy are project decisions, not an extra exchange guarantee or an explicit endorsement by the assignment.

Map mutation costs `O(k log N)` for `k` changed levels and `N` retained levels. Duplicate validation uses a set, adding `O(k log k)`; complete event work is `O(k log k + k log N)`, plus bounded top-five extraction. Memory is `O(N+k)`. This does not satisfy an `O(k)` asymptotic preference. Limits are 10,000 changes per side, 20,000 retained levels per side and the existing 1 MiB WebSocket message limit.

## Commands actually executed

Working directory was the repository root. Commands were launched with Python `subprocess.run` argument arrays, with only the child environment modified:

```text
PATH=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin;<inherited PATH>
SSL_CERT_FILE=C:\Users\hp\.codex\tools\binance-stage4b\ca-bundle.crt
```

The following executable/argument combinations were run (line breaks here are for readability; each was one argument array):

```text
C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\g++.exe
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc
  tests/decimal_tests.cpp src/decimal.cpp
  -o build-stage4d-check/decimal_tests.exe
build-stage4d-check/decimal_tests.exe

C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\cmake.exe
  -S . -B build-stage4d -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=ON
  -DCMAKE_CXX_COMPILER=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\g++.exe
  -DCMAKE_MAKE_PROGRAM=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\ninja.exe
  -DCMAKE_PREFIX_PATH=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64
C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\cmake.exe --build build-stage4d --parallel 2
C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\ctest.exe --test-dir build-stage4d -V --output-on-failure

build-stage4d\binance_capture.exe --venue spot --symbols BTCUSDT
  --output-dir output\stage4d-20261021T032849Z --duration 60

python tests/validate_market_csv.py
  output\stage4d-20261021T032849Z\market_data_spot_BTCUSDT_2026-10-21.csv
  --session-log build-stage4d\live-60.stderr.log
  --samples-log build-stage4d\live-60.stdout.log
python tests/validate_capture_pair.py
  output\stage4d-20261021T032849Z\market_data_spot_BTCUSDT_2026-10-21.csv
  output\stage4d-20261021T032849Z\market_data_spot_BTCUSDT_2026-10-21_orderbook.csv
  --session-log build-stage4d\live-60.stderr.log

git diff --check
git status --short
git rev-parse HEAD
git ls-files --deleted
```

The decimal executable passed before book integration. `build-stage4d` did not exist before configuration, so the initial build was clean. After updating help text, the application was incrementally rebuilt and all tests rerun before live capture. A final test-only addition checked that snapshot replacement discards an old known sixth level; the test executable was rebuilt and all six tests passed again. The application binary used for live capture did not change afterward.

Actual dependencies: GCC 16.2.0 x64, CMake 4.4.4, Ninja 1.13.2, Boost 1.92.0, OpenSSL 3.6.5, nlohmann/json 3.12.0. Existing authorized UCRT64 packages were reused. No global environment settings changed.

## Build and offline results

| Check | Actual result |
| --- | --- |
| Standalone decimal compile/test | Both exit 0; 10 exact-value cases and 15 rejected inputs. |
| Fresh CMake Release configure | Exit 0. |
| Clean build and subsequent incremental builds | Exit 0; no compiler warnings. |
| `envelope_validation` | Passed: 4 classifications and 11 invalid envelopes. |
| `market_data_csv` | Passed: existing nine-column round trips, metadata, timestamps, sequences, exclusive creation and I/O errors. |
| `cli_help` | Passed with Stage 4D usage. |
| `cli_rejects_usdm` | Passed: unsupported venue exited nonzero as expected. |
| `decimal_conversion` | Passed: 25 cases. |
| `order_book_and_pipeline` | Passed: 120 explicit checks, enabled in Release. |
| Final CTest invocation | Exit 0; 6/6 passed, reported total time 0.61 seconds. |
| `git diff --check` | Exit 0; only Git's existing LF-to-CRLF notices, not compiler diagnostics. |

The 120 checks include bid/ask insertion and ordering, absolute replacement, zero deletion and absent-price deletion, multiple changes, replacement clearing old coverage, fresh/stale/equal snapshots, contiguous/overlapping/duplicate/gapped diffs, known/unknown sixth levels, epoch reset, same-ID contradictions, max uint64 revision, malformed complete-message atomicity, short snapshots, crossed states, field types, decimal decoding, side letters, original timestamps including backward time, sequence continuity across epochs, exact output rows, trades not mutating the book, and audit failure preventing book mutation. A separate hand-constructed six-level vector fixture supplies independently known expected top-five values and an unchanged unseen sixth level.

Logs: `build-stage4d/configure.log`, `build.log`, `build-final.log`, `build-tests-final.log`, `tests-final.log`, and CTest's `Testing/Temporary/LastTest.log`. Generated build/output files are intentionally ignored by Git.

## Real 60-second capture

Run started at 03:28:49 UTC / 08:58:49 IST on October 21, 2026. The requested duration includes connection setup, as before. The program reported 60,389 ms including shutdown, exit 0. DNS, TCP, verified TLS and WebSocket handshake succeeded.

| Stream/result | Count |
| --- | ---: |
| Differential audit events | 580 |
| Depth5 audit events | 580 |
| Trade audit events | 164 |
| Total nine-column audit rows | 1,324 |
| Applied snapshots / `P` rows | 580 |
| Applied diffs / `D` rows | 0 |
| Covered/stale diffs | 580 |
| Stale snapshots | 0 |
| Waiting / gaps / coverage losses / crossed candidates | 0 / 0 / 0 / 0 |
| Nonempty same-revision quantity checks | 315 |
| Parse / connection / I/O / book errors | 0 / 0 / 0 / 0 |
| Ping frames | 3 |
| TLS shutdown warnings | 1 |

The warning was the existing narrowly tolerated case: Binance returned normal WebSocket close 1000 but omitted TLS `close_notify` during intentional shutdown. Certificate/hostname verification was enabled. This is a warning in the real run, not a warning-free shutdown claim.

An independent audit scan found **all 580 diffs had `u` equal to the preceding depth5's `lastUpdateId`**. They were already covered, not missing exchange updates. Only 315 had in-range pairs for a nonempty quantity comparison; the other 265 provided no such comparison. There were no same-revision contradictions.

**Design concern:** 100% of differential events were suppressed as covered (50% of all depth messages). The model did not spend time waiting, but this live file supplies no `D`-row reconstruction evidence. The offline fixtures demonstrate diff behavior; they do not establish all cross-stream guarantees. Whether the reviewer expects a row for every received diff, including one already covered by a preceding snapshot, remains an acceptance issue. Do not relabel covered diffs as applied merely to manufacture `D` rows. Review this policy before broader acceptance or a full-sync redesign.

## CSV evidence

Both validators exited 0. Python's independent CSV parser checked all logical field counts. The market validator also checked all three streams, sequence/metadata/JSON, preservation of the three printed source samples and final session counts. The pair validator checked exact book header/order, 26 fields on every row, int64 value bounds, strict bid/ask sorting, positive live quantities, trailing zero pairs, uncrossed tops, consecutive `seqNo`, instrument/type/side, original event timestamps and receive order, exact `P` values against original payloads, and agreement between decision counters and all depth events.

The validator contains a same-revision exchange-snapshot comparison for `D` rows; **zero such comparisons were exercised in this live run**.

Files under `output/stage4d-20261021T032849Z/`:

| File | Data rows | Bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `market_data_spot_BTCUSDT_2026-10-21.csv` | 1,324 | 641,154 | `7e6b66cd088956420b57f7c74aaa90d34e16571a2aa668e12570a900cc5fed4b` |
| `market_data_spot_BTCUSDT_2026-10-21_orderbook.csv` | 580 | 141,235 | `82a7ac44b980fc3b01c9cfb141487c70530341ea6955fb6d75a0f109f0e8d259` |

Run evidence: `build-stage4d/live-60.result.json`, `live-60.stdout.log`, `live-60.stderr.log`, `market-validation.log`, `pair-validation.log`, `final-audit.json`.

## Remaining checks and Stage 4E recommendation

After approval, prioritize final Ubuntu/GCC 12 verification and review the suppressed-diff acceptance question before expanding scope. WSL/Linux was unavailable here; Windows success does not replace Linux verification. A reviewer-length run, controlled network failure tests, sanitizer checks and meaningful live differential-application evidence remain outstanding. No performance measurements or complete latency/throughput claims were made.

The limited range proof depends on comparable snapshot/diff revisions and correct exchange input. No finite sample proves that assumption universally. A full REST/bootstrap implementation would be a significant separately approved change. Map complexity remains a documented trade-off. Two CSVs are not transactional or crash-durable per event; writes are buffered and file failures stop the run. Network failures stop rather than reconnect. These limitations are unchanged by a successful capture.

Stage 4D stops here for user review. Stage 4E, commits and pushes require further authorization.
