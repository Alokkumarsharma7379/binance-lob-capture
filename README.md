# Binance Market Data Capture and Local Order Book

A C++17 application for the iRage Software Developer Internship assignment. It captures Binance Spot **BTCUSDT** through one combined WebSocket connection and produces two CSV files: a market-event audit and the reconstructed top five bids and asks.

The application subscribes to differential depth, depth5 snapshots and individual trades. Its local book uses a limited-depth model anchored by depth5 snapshots. Deterministic offline replay regenerates the order-book CSV from the saved audit **without network calls**.

## Key features

- **Asynchronous networking:** Boost.Asio/Beast handles connection setup, complete-message reads, ping/pong, timers and shutdown on one event loop.
- **Verified TLS:** OpenSSL certificate-chain and hostname verification, SNI and a TLS 1.2 minimum.
- **Auditable processing:** recognized market events are written before book mutation, including stale and unapplied depth events.
- **Exact decimals:** checked price and quantity conversion to scaled `int64_t`, without floating-point arithmetic.
- **Explicit book state:** ordered levels, absolute quantity updates, zero deletion, revision checks and snapshot recovery after lost continuity or depth coverage.
- **Reproducible CSVs:** fixed schemas, RFC 4180-style escaping, checked file finalization and shared live/replay book logic.

## Architecture and data flow

```text
Binance combined WebSocket
    |
Session -> envelope parsing -> market-data audit CSV
                                   |
                          shared event processing
                                   |
                          depth decoding / scaling
                                   |
                              local book -> order-book CSV

Recorded market CSV -> CSV reader / replay -> shared event processing
```

Trades are audited without applying execution quantities to the book. Every successfully applied depth event emits a book row, even if the visible top five is unchanged. Epoch resets also apply to recorded trade events, as described below.

| Modules under `src/` | Responsibility |
| --- | --- |
| [main.cpp](src/main.cpp), [config.cpp](src/config.cpp) | Validate CLI options and select live capture or replay. |
| [session.cpp](src/session.cpp), [envelope.cpp](src/envelope.cpp) | Own network operations, timestamp complete reads, classify streams and parse inner JSON. |
| [market_data.cpp](src/market_data.cpp), [csv_file.cpp](src/csv_file.cpp) | Serialize the audit; own exclusively created, buffered CSV files. |
| [event_processing.cpp](src/event_processing.cpp) | Coordinate audit-before-mutation and shared book processing. |
| [depth_event.cpp](src/depth_event.cpp), [decimal.cpp](src/decimal.cpp) | Validate depth fields and convert decimal strings. |
| [order_book.cpp](src/order_book.cpp), [order_book_csv.cpp](src/order_book_csv.cpp) | Maintain levels, enforce sequence/coverage rules and serialize the top five. |
| [csv_reader.cpp](src/csv_reader.cpp), [replay.cpp](src/replay.cpp) | Validate recorded CSV events and replay them offline. |

One thread preserves processing order and keeps ownership straightforward. Parsing, map updates and buffered `FILE` writes execute in the event handlers. Files flush every second and on checked close. Slow storage can delay networking and timers; there is no writer thread or transaction across the two outputs.

## Build and dependencies

Requires a C++17 compiler, **CMake 3.16+**, **Boost headers 1.74+**, **OpenSSL development libraries 1.1.1+** and **nlohmann/json 3.10+**. OpenSSL 3 is recommended. CMake discovers installed dependencies and downloads nothing. GCC builds enable `-Wall -Wextra -Wpedantic` on application and test targets.

| Verified environment | Compiler and build tools | Libraries | Release result |
| --- | --- | --- | --- |
| Windows x64, isolated UCRT64 | GCC 16.2.0, CMake 4.4.4, Ninja 1.13.2 | Boost 1.92.0, OpenSSL 3.6.5, JSON 3.12.0 | Clean build; 8/8 CTest tests passed. |
| Ubuntu 26.04.1 / WSL2, x86_64 | GCC 15.2.0, CMake 4.2.3, GNU Make 4.4.1 | Boost 1.90.0, OpenSSL 3.5.5, JSON 3.11.3 | Clean build; 8/8 CTest tests passed. |

Live capture was verified on Windows. Linux validation covers compilation, offline tests and replay. GCC 12, used in the assignment's example, and Clang have not been verified. Run commands from the repository root.

### Ubuntu / WSL

Install dependencies from Ubuntu's repositories, then configure and build:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends g++ cmake make libboost-dev libssl-dev nlohmann-json3-dev ca-certificates

cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DBUILD_TESTING=ON
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
```

### Windows / PowerShell

Use an existing x64 UCRT64 environment containing GCC, CMake, Ninja, Boost, OpenSSL and nlohmann/json. Compiler, libraries and runtime DLLs must belong to the same toolchain. Set `$capturePrefix` to its `ucrt64` directory; the example location is configurable.

```powershell
$capturePrefix = 'C:\msys64\ucrt64' # Change to your existing UCRT64 directory.
$env:PATH = "$capturePrefix\bin;$env:PATH"

& "$capturePrefix\bin\cmake.exe" -S . -B build-windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
  "-DCMAKE_CXX_COMPILER=$capturePrefix/bin/g++.exe" `
  "-DCMAKE_MAKE_PROGRAM=$capturePrefix/bin/ninja.exe" `
  "-DCMAKE_PREFIX_PATH=$capturePrefix"
& "$capturePrefix\bin\cmake.exe" --build build-windows --parallel 2
& "$capturePrefix\bin\ctest.exe" --test-dir build-windows --output-on-failure
```

The tested setup used isolated UCRT64 packages outside this repository. The `PATH` change affects only this PowerShell session and its children. The optional `.vscode` configuration contains local editor paths; adapt them to your installation. It does not control CMake.

## Run the application

### Live capture

Linux:

```sh
./build-linux/binance_capture --venue spot --symbols BTCUSDT --output-dir ./output/live-run --duration 60
```

Windows, after setting the toolchain `PATH`:

```powershell
$env:SSL_CERT_FILE = 'C:\path\to\trusted-ca-bundle.crt' # Existing trusted PEM bundle.
.\build-windows\binance_capture.exe --venue spot --symbols BTCUSDT --output-dir ./output/live-run --duration 60
```

The tested portable Windows installation had empty default CA files and required `SSL_CERT_FILE` to select a trusted PEM bundle. The successful run used an existing Git for Windows bundle containing 150 certificates; [the TLS investigation](docs/stage4f-phase1-2.md) records its provenance and hash. No bundle is shipped here. Linux normally uses its installed OpenSSL trust store. Verification remains enabled; there is no insecure fallback.

Use a fresh output directory for each run. Missing directories are created; existing target CSVs are refused. Duration includes connection setup; omit it to run until a signal or error. Ctrl+C/SIGINT, SIGTERM and Windows Ctrl+Break request orderly shutdown. Connection/handshake deadlines are 15 seconds, the WebSocket idle timeout is 60 seconds, and network shutdown has a five-second deadline.

### Command-line options

| Option | Behavior |
| --- | --- |
| `--help`, `-h` | Show usage. |
| `--venue spot` | Default and only supported venue. |
| `--symbols BTCUSDT` | Default and only symbol; other symbols and comma-separated lists are rejected. |
| `--output-dir PATH` | Default `./output`; create directories and refuse existing output files. |
| `--duration SECONDS` | Positive integer within the timer range; incompatible with replay. |
| `--replay CSV` | Read a market audit and regenerate only its book CSV, entirely offline. |

Use separate option/value arguments. Unknown or duplicate options, missing values and invalid combinations exit **2**, including when combined with `--help`. Runtime failures exit **1**. A live run exits **0** only after a requested stop, all three streams, at least one book row and successful finalization of both files. A completed valid replay exits **0**; a header-only audit is a valid empty replay.

### Offline replay

Replay reads physical CSV order, retains stored timestamps and uses the same depth decoder, book policy and formatter as live capture. It makes **no network calls**, constructs no TLS session and does not sleep to reproduce arrival timing.

```sh
./build-linux/binance_capture --replay samples/market_data_spot_BTCUSDT_2026-10-21.csv --output-dir ./output/sample-replay
cmp samples/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv output/sample-replay/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv
```

Windows equivalent:

```powershell
.\build-windows\binance_capture.exe --replay samples/market_data_spot_BTCUSDT_2026-10-21.csv --output-dir ./output/sample-replay
& "$capturePrefix\bin\cmake.exe" -E compare_files samples/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv output/sample-replay/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv
```

Each example needs a fresh output directory. Replay writes `<input-stem>_orderbook.csv` and does not rewrite the audit. It validates the exact header, quoted fields, supported metadata, minified JSON and sequence order. LF/CRLF and a final record without a newline are accepted. Malformed input exits 1 with a logical record number and may leave a valid earlier output prefix. Identical input under the same policy produces byte-identical book output.

## Input streams and CSV formats

Combined endpoint:

```text
wss://stream.binance.com:9443/stream?streams=btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade
```

| Subscription | Audit `stream_kind` | Use |
| --- | --- | --- |
| `btcusdt@depth@100ms` | `depth_diff` | Absolute quantity changes with exchange update IDs `U/u`. |
| `btcusdt@depth5@100ms` | `depth5` | Replacement top-five snapshot with `lastUpdateId`. |
| `btcusdt@trade` | `trade` | Record executions; no execution-based book update or book row. |

The combined envelope is parsed once. The audit stores its minified inner `data` object, retaining decimal strings and unknown fields. Stream identifiers and any supplied payload symbol are checked. Recognized payloads are audited before book mutation, including stale or unapplied depth candidates. Malformed envelopes cannot produce an audit row; fatal depth errors stop after their audit row.

One pair is created per run using its **UTC start date**; the same files remain open across midnight:

```text
market_data_spot_BTCUSDT_<YYYY-MM-DD>.csv
market_data_spot_BTCUSDT_<YYYY-MM-DD>_orderbook.csv
```

### Market audit: nine columns

```csv
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
```

| Field | Meaning in this implementation |
| --- | --- |
| `recv_tsec`, `recv_tnsec` | Local wall-clock time sampled immediately after a complete read: signed int64 seconds and int32 nanosecond remainder in `[0,999999999]`. Binance `E/T` fields do not supply row timestamps. |
| `venue`, `symbol`, `shard_id` | Fixed to `spot`, `BTCUSDT`, `0`. |
| `stream_kind` | `depth_diff`, `depth5` or `trade`. |
| `conn_epoch` | Connection generation; live capture uses 0 because it does not reconnect. Replay supports increasing recorded epochs. |
| `conn_seq` | Checked uint64 message counter, starting at 1; advances for complete text messages before routing. A server control notice can consume a number without an audit row. |
| `payload_json` | Minified inner Binance object, escaped as one CSV field. |

Physical row order is processing order, including equal or backward wall-clock timestamps. Duration and timeout timers use a steady clock.

### Order book: 26 columns

```csv
tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,ask0,ask1,ask2,ask3,ask4,ask_size0,ask_size1,ask_size2,ask_size3,ask_size4
```

| Fields | Meaning |
| --- | --- |
| `tsec`, `tnsec` | Original receive timestamp of the triggering event. |
| `seqNo` | Checked uint64 counter, starting at 1 and incremented once per emitted row; continues across recorded epoch changes. |
| `id` | Stable instrument ID `1` for the sole supported symbol. |
| `type` | `P` for an applied depth5 replacement; `D` for an applied diff. |
| `side` | `B` for bid-only diffs, `S` for ask-only diffs, `N` for both/neither and all snapshots. Classification uses the event's supplied sides. |
| `bid0` ... `bid4`, `bid_size0` ... `bid_size4` | Bid prices descending, with corresponding quantities. |
| `ask0` ... `ask4`, `ask_size0` ... `ask_size4` | Ask prices ascending, with corresponding quantities. |

Prices and quantities are signed **int64 integers scaled by 100000000**: `"101.25"` becomes `10125000000`; `"0.003"` becomes `300000`. Checked parsing rejects malformed syntax, overflow and nonzero precision beyond eight places. Extra trailing zeros are accepted. Short snapshots use trailing zero price/size pairs for missing slots.

Both CSVs use UTF-8, CRLF and RFC 4180-style escaping: fields containing comma, quote, CR or LF are quoted, with internal quotes doubled. Count audit columns with a CSV-aware parser; JSON commas belong to the quoted payload field.

## Order-book reconstruction

Policy **`limited-top5-v2`** uses descending bid and ascending ask `std::map` containers. It retains known levels beyond the exported five while they remain inside the anchor's covered price ranges.

1. **Anchor or refresh:** accept depth5 only if its revision is at least the highest valid depth ID already seen in the epoch. Replace both maps. A complete five-by-five snapshot establishes a bid floor and ask ceiling; a short snapshot emits its supplied levels but leaves diff reconstruction waiting.
2. **Check continuity:** for committed revision `L`, a diff with `u <= L` is covered/stale. When `u > L`, apply only if `U <= L+1 <= u`. A gap (`U > L+1`) clears readiness and waits for a sufficiently fresh snapshot. Equal-revision snapshot or in-range diff contradictions are fatal.
3. **Apply absolute quantities:** replace or insert each in-range level; zero erases it. Outside-range changes are validated but not retained. Never subtract trade quantities separately.
4. **Check coverage:** require at least five known levels per side and best bid below best ask. A known sixth can enter the exported five. If an unknown sixth would be needed, or the candidate is crossed, clear the book and await a fresh snapshot instead of publishing incomplete state.
5. **Emit:** every applied snapshot or diff produces one row. Covered/stale/waiting events stay audit-only. Extract the first five entries of each sorted map; no whole-book sorting is needed.

A refresh discards previous deeper knowledge rather than overlaying it. No update buffering or reordering occurs. Recovery retains the highest-seen watermark so an older snapshot cannot rewind state.

This is a **limited top-five reconstruction model**, distinct from Binance's full REST-snapshot/buffered-diff procedure. Comparable snapshot/diff revisions and coherent input remain integration assumptions; one combined connection does not guarantee a globally ordered bundle of streams. The model can suppress diff output while coverage is unavailable. [The working design](docs/design.md) explains that boundary and the known price ranges: bids at or above the anchor's fifth bid, asks at or below its fifth ask.

For `k` changes and `N` retained levels, map work costs `O(k log(N+k))`; duplicate validation adds `O(k log k)`. Top-five extraction is `O(5)` per side. Snapshot replacement also clears the previous `N` entries, so it costs `O(N)` plus bounded five-level validation/insertion per side. Working memory is `O(N+k)`, excluding JSON/message storage. These are explicit trade-offs against the assignment's `O(updates)` preference.

Bounds: 1 MiB per WebSocket message, 10,000 changes per side, 20,000 retained levels per side and 2 MiB + 4096 bytes per replay record. The retained-level limit is checked after a candidate batch; exceeding it invalidates the book and stops the run.

Network failure stops with exit 1; **automatic reconnect is not implemented**. During replay, an increased `conn_epoch` resets levels, revisions and readiness, even when its first event is a trade. Epochs cannot decrease; `conn_seq` must strictly increase within an epoch, though gaps are allowed. Exchange `U/u`, connection `conn_seq`, generation `conn_epoch` and output `seqNo` have distinct roles.

## Testing and validation

The verified Windows and Linux Release builds each passed **all eight CTest tests**, with no compiler warnings. Tests cover envelopes, CSV serialization, CLI handling, exact decimals, book transitions, replay validation and actual replay CLI execution. Release checks include 25 decimal cases, 120 book/pipeline checks and 115 replay/reliability checks.

The independently specified [27-event regression fixture](tests/fixtures/stage4e_market.csv) produces [15 expected book rows](tests/fixtures/stage4e_expected_orderbook.csv): nine `P` and six `D`. It covers uncovered/overlapping diffs, deletion, duplicates, stale snapshots, gaps, recovery and epoch resets. This synthetic fixture is separate from the live samples.

The successful **60-second Windows capture on October 21, 2026** exited 0 after 60,403 ms, including shutdown:

| Result | Observed |
| --- | ---: |
| Market rows | 2,431: 590 diffs, 590 depth5 snapshots, 1,251 trades |
| Book rows | 590 |
| Fatal parse / connection / I/O / book errors | 0 / 0 / 0 / 0 |
| TLS shutdown warnings | 1 |
| Independent market and paired-book CSV validators | Both passed |
| Full-capture offline replay | Byte-identical book output on Windows and Linux |

All 590 book rows were **`P`**: every diff followed a snapshot with the same final exchange revision and was already covered. The 443 nonempty in-range quantity comparisons agreed. Applied `D` output is demonstrated by the regression fixture, not by this capture.

The shutdown warning records missing TLS `close_notify` after an intentional normal WebSocket close (1000). Only that narrow case is tolerated; truncated live reads remain failures. [Final validation](docs/stage4f-final-validation.md) records commands, hashes and the audit. Its checkpoint/publication notes describe the state at the time of validation.

For a lightweight audit-schema check, with Python 3 installed:

```sh
python3 tests/validate_market_csv.py samples/market_data_spot_BTCUSDT_2026-10-21.csv
```

Python is used only by development validators; the application and offline replay are C++.

## Sample outputs

| File | Data rows |
| --- | ---: |
| [Market audit](samples/market_data_spot_BTCUSDT_2026-10-21.csv) | 225: 20 depth5, 20 diffs, 185 trades |
| [Corresponding order book](samples/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv) | 20 |

These are genuine, unmodified, contiguous byte prefixes of the successful capture, covering the same 1.9610324-second interval. Every event in the prefix and corresponding book row is preserved. They are excerpts, not the full minute. [Sample provenance](samples/README.md) includes timestamps and SHA-256 hashes. Use the offline replay/comparison commands above to reproduce the pair. Large captures and build products remain ignored.

## Known limitations

- One Binance Spot BTCUSDT connection only; no USD-M or multiple-symbol capture.
- Limited-range reconstruction can pause output after lost coverage. Full REST synchronization and automatic reconnect are absent.
- A separate 120-second attempt received about 49 seconds of events, then hit the 60-second idle timeout and exited 1 after 110,093 ms. The cause of traffic silence remains unresolved; sustained 120-/300-second capture is unverified. The CLI accepts `--duration 300`.
- File I/O shares the network event loop; buffered output is neither transactional nor guaranteed durable after a crash.
- No verified production latency/throughput benchmark is claimed. The published validation does not establish sanitizer coverage or leak freedom.

## Repository and version

Repository: [Alokkumarsharma7379/binance-lob-capture](https://github.com/Alokkumarsharma7379/binance-lob-capture).

Published submission: annotated tag [v1.0.0](https://github.com/Alokkumarsharma7379/binance-lob-capture/tree/v1.0.0), pointing to commit [fbf77a1](https://github.com/Alokkumarsharma7379/binance-lob-capture/commit/fbf77a15b3e2bf85c5c37e1ee93154a5e551ed93). The tag identifies the submitted implementation and recorded validation. [Assignment requirements](docs/requirements.md) preserve the original contract and documented interpretations.
