# Binance market-data capture and local order book

C++17 application for Binance Spot **BTCUSDT**, using one combined WebSocket connection. It writes a nine-column market-data audit CSV and a 26-column top-five order-book CSV. Offline replay regenerates the book CSV without network calls.

The book is a limited model anchored by depth5 snapshots. It pauses differential output when continuity or known depth is lost. Full-depth REST synchronization, automatic reconnect, USD-M and multiple symbols are not implemented.

## Build

Dependencies: CMake 3.16+, a C++17 compiler, Boost headers 1.74+, OpenSSL development libraries 1.1.1+ (OpenSSL 3 recommended), and nlohmann/json 3.10+. CMake discovers installed packages and downloads nothing. Warning flags are enabled on each target.

| Tested environment | Tools | Result |
| --- | --- | --- |
| Windows x64, isolated MSYS2 UCRT64 packages | GCC 16.2.0, CMake 4.4.4, Ninja 1.13.2, Boost 1.92.0, OpenSSL 3.6.5, JSON 3.12.0 | Clean Release build; 8/8 CTest tests passed. |
| Ubuntu 26.04.1 under WSL2 | GCC 15.2.0, CMake 4.2.3, GNU Make 4.4.1, Boost 1.90.0, OpenSSL 3.5.5, JSON 3.11.3 | Clean Release build; 8/8 CTest tests passed. |

Live TLS/WebSocket capture was verified on Windows; Linux verification covers compilation, offline tests and replay.

GCC 12 is the assignment's example compiler, but it is not available in the configured Ubuntu repositories and has not been tested. No Clang result is claimed.

### Linux / WSL2

The following dependencies were installed from Ubuntu's official repositories:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends g++ cmake make libboost-dev libssl-dev nlohmann-json3-dev ca-certificates
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DBUILD_TESTING=ON
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
```

### Windows PowerShell

These commands use the existing isolated UCRT64 installation. Set `$capturePrefix` to your compatible installation; compilers, libraries and runtime DLLs must use the same architecture/toolchain. Dependencies are outside this repository. The path below is a local setup example, not a required installation location.

```powershell
$capturePrefix = "$env:USERPROFILE\.codex\tools\binance-stage4b\ucrt64"
$env:PATH = "$capturePrefix\bin;$env:PATH"
& "$capturePrefix\bin\cmake.exe" -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DCMAKE_CXX_COMPILER=$capturePrefix/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$capturePrefix/bin/ninja.exe" "-DCMAKE_PREFIX_PATH=$capturePrefix"
& "$capturePrefix\bin\cmake.exe" --build build-windows --parallel 2
& "$capturePrefix\bin\ctest.exe" --test-dir build-windows --output-on-failure
```

Changes to `$env:PATH` affect that PowerShell process and its children, not the system configuration. The optional VS Code configuration points to this local toolchain and `build-windows/compile_commands.json`; adapt it to your installation. It does not control CMake builds.

## Run live capture

Linux:

```sh
./build-linux/binance_capture --venue spot --symbols BTCUSDT --output-dir ./output/live-run --duration 60
```

Windows, after setting the toolchain PATH above:

```powershell
$env:SSL_CERT_FILE = "$env:USERPROFILE\.codex\tools\binance-stage4b\ca-bundle.crt"
.\build-windows\binance_capture.exe --venue spot --symbols BTCUSDT --output-dir ./output/live-run --duration 60
```

`SSL_CERT_FILE` must identify an existing trusted PEM CA bundle. The tested portable installation has empty default CA files; the explicitly selected bundle was copied from the existing Git for Windows installation and contains 150 CA certificates. Its provenance and verified hash are in [the TLS investigation](docs/stage4f-phase1-2.md). No CA bundle is shipped in this repository. Linux normally uses its installed OpenSSL trust store. Certificate-chain verification, hostname verification, SNI and TLS 1.2+ remain enabled; there is no insecure fallback.

Use a fresh output directory for each run. Duration includes connection setup; omit it to run until interrupted or an error occurs. `--duration 300` is accepted for the assignment's reviewer invocation, but a successful 300-second run has not been demonstrated.

| Option | Supported behavior |
| --- | --- |
| `--help`, `-h` | Show usage. |
| `--venue spot` | Default and only supported venue. |
| `--symbols BTCUSDT` | Default and only supported symbol; lists/other symbols are rejected. |
| `--output-dir PATH` | Default `./output`; create directories, refuse existing targets. |
| `--duration SECONDS` | Positive integer; incompatible with replay. |
| `--replay CSV` | Regenerate only the book CSV, entirely offline. |

Use separate option/value arguments. Unknown/duplicate options, missing values and invalid combinations exit 2, including when supplied with `--help`. Capture failures exit 1. A requested duration/signal stop exits 0 only after all three streams, at least one book row and successful finalization of both CSVs. SIGINT/Ctrl+C, SIGTERM and Windows Ctrl+Break request orderly shutdown.

## Input streams and output files

Endpoint: `wss://stream.binance.com:9443/stream?streams=btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade`.

| Stream | Audit `stream_kind` | Meaning |
| --- | --- | --- |
| `btcusdt@depth@100ms` | `depth_diff` | Absolute price-level quantity changes, with exchange update IDs `U/u`. |
| `btcusdt@depth5@100ms` | `depth5` | Replacement top-five snapshot with `lastUpdateId`. |
| `btcusdt@trade` | `trade` | Executed trades; audited without subtracting quantities from the book. |

The combined envelope is parsed once. `payload_json` stores its minified inner `data` object, preserving decimal strings and unknown fields. Supported stream identifiers and supplied symbols are checked. Every recognized market payload is audited before any book mutation, including stale/rejected depth candidates. An unrecoverable semantic error stops after its audit row.

One pair is created per run, using the UTC run-start date even across midnight:

```text
market_data_spot_BTCUSDT_<YYYY-MM-DD>.csv
market_data_spot_BTCUSDT_<YYYY-MM-DD>_orderbook.csv
```

Exact market header, **nine logical columns**:

```csv
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
```

`recv_tsec` is int64 seconds and `recv_tnsec` an int32 remainder in `[0,999999999]`, taken from local wall-clock time immediately after a complete read. Physical row order is processing order, even if the clock moves backward. Duration/timeouts use a steady clock. `venue=spot`, `symbol=BTCUSDT`, shard and live epoch are 0. The checked uint64 `conn_seq` starts at 1 per connection and advances for each complete text message; a control notice can consume a number without an audit row.

Exact book header, **26 columns**:

```csv
tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,ask0,ask1,ask2,ask3,ask4,ask_size0,ask_size1,ask_size2,ask_size3,ask_size4
```

Book time is the original triggering receive timestamp. `seqNo` is uint64, starts at 1, and increments only for emitted rows. Instrument `id=1` identifies the single supported symbol. Type `P` is an applied depth5 replacement; `D` is an applied diff. Side is `B` for bid-only diffs, `S` for ask-only diffs, and `N` for both/neither or any snapshot. Bid prices descend; ask prices ascend; quantities correspond by index.

Prices and quantities are signed int64 integers scaled by **100000000**: `"101.25"` becomes `10125000000`; `"0.003"` becomes `300000`. Conversion uses checked decimal arithmetic without `double`. Malformed syntax, overflow and nonzero precision beyond eight decimal places are rejected; additional trailing zeros are accepted. Both CSVs use UTF-8, CRLF and RFC 4180-style quoting: fields containing comma, quote, CR or LF are quoted and internal quotes doubled.

## Reconstruction, sequencing and limits

Policy `limited-top5-v2` uses descending/ascending `std::map` containers. A fresh depth5 replaces both maps. Its fifth prices establish the known ranges: bids at or above the bid floor and asks at or below the ask ceiling. Retain all known levels in those ranges, including a known sixth. Diffs replace quantities; zero deletes. Outside-range pairs are validated but not retained.

For current revision `L`, a diff with `u <= L` is already covered. Otherwise it can advance when `U <= L+1 <= u`. A gap (`U > L+1`), crossed candidate or insufficient known depth clears readiness and waits for a snapshot at least as fresh as the highest observed valid depth ID. Stale snapshots cannot rewind the book. Equal-revision contradictions are fatal, including short-snapshot contradictions. No update buffering or reordering is performed.

Every successfully applied depth event emits a row, even when the visible five are unchanged. Covered/stale/waiting candidates remain audit-only and are counted. Short snapshots emit their supplied levels with trailing zero price/size pairs, but do not enable diff reconstruction. An unknown sixth after deletion invalidates the view rather than being hidden by zero padding. Trades emit no book rows.

The model assumes comparable snapshot/diff revisions and coherent exchange input. It is not Binance's complete REST-bootstrap procedure. Cross-stream arrival order is not guaranteed. Loss of coverage can suppress otherwise valid diffs, and covered-diff suppression remains an explicit reviewer-acceptance limitation. [The working design](docs/design.md) details the proof boundary and examples.

There is **no automatic reconnect**. Network failure stops with exit 1 and invalidates the book. In recorded replay, increasing `conn_epoch` resets levels/revisions/readiness, including when the first new-epoch event is a trade; book `seqNo` continues. Decreasing epochs and duplicate/backward `conn_seq` within an epoch fail; gaps in `conn_seq` are allowed. Exchange IDs, connection sequence, epoch and output sequence have distinct meanings.

Map mutation is `O(k log N)`; duplicate validation adds `O(k log k)`. This does not meet an `O(k)` asymptotic target. Limits: one symbol/shard, 1 MiB WebSocket message, 10,000 changes per side and 20,000 retained levels per side. No throughput/latency benchmark is claimed.

One thread runs the Asio event loop; decoding and buffered `FILE` writes run in order. Files flush every second and on checked final close. Slow storage delays networking/timers; there is no writer thread or crash-transaction guarantee. Handshake timeout is 15 seconds, WebSocket idle timeout 60 seconds and close deadline five seconds. Beast handles fragments and automatic pong replies. Missing TLS `close_notify` is reported only as a warning after an intentional normal WebSocket close (1000); truncated live reads remain failures.

## Replay and tests

Replay creates no Session, TLS context or network calls, does not sleep, and preserves stored timestamps/event order. It validates the exact audit schema and current Spot BTCUSDT/shard-0 metadata, then uses the shared decoder/book/writer. It accepts proper quoted fields, LF/CRLF and a final record without a newline; raw records are bounded to 2 MiB + 4096 bytes. Invalid input exits 1 with a logical record number and may leave a valid earlier output prefix. A header-only input is a valid empty replay. Fixed input and policy yield byte-identical output.

Linux, from the repository root:

```sh
./build-linux/binance_capture --replay samples/market_data_spot_BTCUSDT_2026-10-21.csv --output-dir ./output/sample-replay
cmp samples/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv output/sample-replay/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv
ctest --test-dir build-linux --output-on-failure
```

Windows uses `.\build-windows\binance_capture.exe` and a fresh `--output-dir`; `cmake -E compare_files EXPECTED ACTUAL` compares bytes. Output name is `<input-stem>_orderbook.csv`. Replay never writes another audit file or overwrites an existing book file.

Eight CTest tests cover envelope validation, nine-column CSV behavior, CLI validation, decimal conversion, book/pipeline transitions, replay reliability and actual CLI replay. Explicit checks remain enabled in Release: 25 decimal cases, 120 book checks and 115 replay/reliability checks. The independent 27-event fixture expects 15 rows (9 `P`, 6 `D`), exercising uncovered/overlapping diffs, deletion, duplicates, stale snapshots, gaps, recovery and epoch reset. It is synthetic test data, separate from genuine samples.

For development CSV checking (the application itself requires no Python runtime):

```sh
python3 tests/validate_market_csv.py samples/market_data_spot_BTCUSDT_2026-10-21.csv
```

## Genuine capture and remaining limitations

The verified 60-second Windows run on October 21, 2026 exited 0 after 60,403 ms: **2,431 market rows** (590 diffs, 590 depth5, 1,251 trades) and **590 book rows**, zero fatal errors and one TLS shutdown warning. All 590 diffs followed snapshots with the same final revision, so output contains 590 `P` rows and no `D` rows. In-range quantities agreed in 443 nonempty comparisons. Offline replay reproduced the full book byte for byte; the fixture independently validates `D` output.

[The genuine sample pair](samples/README.md) is an unchanged contiguous two-second prefix: 225 market rows and all 20 corresponding book rows. It is deliberately labeled an excerpt, preserves every event within its interval, and can be replayed/compared using the commands above. Large captures/build products remain ignored; only the explicit sample pair and fixtures are allowed by `.gitignore`.

A separate 120-second attempt received about 49 seconds of events, then hit the configured 60-second idle timeout and exited 1 after 110,093 ms. The timeout policy explains the termination; the cause of missing peer traffic is unresolved. A stable 120-/300-second run is not claimed. We retained this result rather than weakening TLS, increasing timeouts blindly or claiming transport stability. Full-depth synchronization, automatic reconnect and multi-symbol/USD-M support remain absent. Windows ASan/UBSan runtimes were unavailable; sanitizer/leak freedom is not claimed.

[Final validation](docs/stage4f-final-validation.md) records exact commands, Linux results, hashes, schema/reconciliation checks and the repository audit. Historical phase reports retain machine-local paths as evidence, not required installation locations. [Assignment requirements](docs/requirements.md) preserve the original contract and unresolved interpretations.

## How to submit on GitHub

Repository: [Alokkumarsharma7379/binance-lob-capture](https://github.com/Alokkumarsharma7379/binance-lob-capture). Current reviewed checkpoint is `31165a9` on `main`. Final preparation does not commit, push, tag or publish automatically.

After reviewing the pending diff and approving submission, the intended commands are:

```sh
git status --short
git add README.md .gitignore .gitattributes .vscode/c_cpp_properties.json docs/stage4f-final-validation.md samples/
git diff --cached --check
git diff --cached --stat
git commit -m "Finalize validation, documentation and genuine capture samples"
git push origin main
git tag -a v1.0.0 -m "Submission v1.0.0"
git push origin v1.0.0
```

Review the diff before executing these commands. Build artifacts, toolchains, large generated CSVs and credentials must remain excluded. Share the repository URL and the approved `v1.0.0` tag after successful pushes.
