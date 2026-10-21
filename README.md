# Binance market-data capture

The application captures Binance Spot BTCUSDT depth, depth5 and trade events over one verified TLS WebSocket. It writes the nine-column audit CSV and a 26-column limited top-five book CSV. Fixed-point conversion, map updates and conservative snapshot recovery are implemented. Stage 4E adds deterministic offline replay. Full-depth REST synchronization and automatic reconnect remain unimplemented. The reported TLS certificate verification failure is reserved for Stage 4F; no live capture was attempted in Stage 4E.

## Linux build

Requirements: C++17 (GCC 12 recommended), CMake 3.16+, Boost headers 1.74+, OpenSSL development files 1.1.1+ (supported OpenSSL 3 recommended), and nlohmann/json 3.10+ with its CMake package. CMake discovers local dependencies; it does not download them.

These Ubuntu commands are provided for final Linux verification and have not been run in the current Windows environment:

```sh
sudo apt-get update
sudo apt-get install -y g++-12 cmake ninja-build libboost-dev libssl-dev nlohmann-json3-dev ca-certificates
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-12
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
./build-linux/binance_capture --venue spot --symbols BTCUSDT --output-dir ./output/new-run --duration 60
```

## Tested portable Windows build

The isolated toolchain is outside this repository at `C:\Users\hp\.codex\tools\binance-stage4b`. GCC 16.2.0, CMake 4.4.4, Ninja 1.13.2, Boost 1.92.0, OpenSSL 3.6.5 and JSON 3.12.0 were used. Official MSYS2 UCRT64 packages and required runtime dependencies were downloaded with user approval and archive SHA-256 verification. `installed-manifest.json` in that folder records versions, sources and hashes. No system PATH, registry or global environment settings were modified.

From the project root in PowerShell (environment changes affect only that process and its children):

```powershell
$stageTools = 'C:\Users\hp\.codex\tools\binance-stage4b'
$stagePrefix = "$stageTools\ucrt64"
$env:PATH = "$stagePrefix\bin;$env:PATH"
$env:SSL_CERT_FILE = "$stageTools\ca-bundle.crt"
& "$stagePrefix\bin\cmake.exe" -S . -B build-stage4e -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=$stagePrefix/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$stagePrefix/bin/ninja.exe" "-DCMAKE_PREFIX_PATH=$stagePrefix"
& "$stagePrefix\bin\cmake.exe" --build build-stage4e --parallel 2
& "$stagePrefix\bin\ctest.exe" --test-dir build-stage4e --output-on-failure
.\build-stage4e\binance_capture.exe --venue spot --symbols BTCUSDT --output-dir ./output/new-run --duration 60
```

The extracted CA package's generated bundle was empty without its installation hook. A nonempty CA bundle from the existing Git for Windows installation was copied to `$stageTools\ca-bundle.crt`; `ca-source.json` records its source and hash. Certificate-chain and hostname verification remain enabled. Linux uses its installed OpenSSL CA roots by default; `SSL_CERT_FILE` may select an explicitly trusted PEM bundle where needed.

## CLI and behavior

| Option | Behavior |
| --- | --- |
| `--help`, `-h` | Show usage; exit 0. |
| `--venue spot` | Default `spot`; other venues rejected. |
| `--symbols BTCUSDT` | Default `BTCUSDT`; other symbols/lists rejected. |
| `--output-dir PATH` | Default `./output`; create directories, refuse an existing capture file. |
| `--replay CSV` | Regenerate only the book CSV from an existing audit; no network. Cannot combine with duration. |
| `--duration SECONDS` | Positive integer within timer range; includes connection setup. Omit to run until interrupted or an error occurs. |

Use separate option/value arguments. Unknown/duplicate options, missing values and invalid durations exit 2. All arguments are checked even with `--help`. Connection, envelope, file and incomplete-capture failures exit 1. A duration/signal stop exits 0 only after all three streams were observed without a fatal error and at least one book row was emitted and both CSVs were finalized. No reconnect or insecure TLS fallback exists.

SIGINT/Ctrl+C and SIGTERM request orderly close; Windows Ctrl+Break is also supported. Cleanup waits at most five seconds for WebSocket/TLS close. Beast handles ping/pong and fragmentation. Missing TLS `close_notify` is an explicit warning only after a normal peer WebSocket close code 1000 during an intentional stop. Truncated live reads and unconfirmed closes remain failures.

The endpoint is exactly `wss://stream.binance.com:9443/stream?streams=btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade`. Parse once, validate the exact stream and object-valued `data`, and check `s` against BTCUSDT when supplied. Depth5's symbol comes from its subscription. Preserve all inner fields and decimal strings; the parsed inner object is retained for depth decoding after its audit row is written. Samples remain intact. Messages received solely for the closing handshake after stop are outside capture counters.

## Market CSV policy

The filename is `market_data_spot_BTCUSDT_<UTC-date>.csv`, using the UTC run-start date, retained across midnight. Existing files are refused without modification; choose another output directory for another run on the same date. A failed connection may leave a header-only file.

```text
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
```

Capture local wall-clock time immediately after a complete read, before parsing. Split it into integer seconds and a nonnegative nanosecond remainder; clock resolution depends on the OS. Physical row order is processing order, even if the clock moves backward. Duration and flush timers use a monotonic clock. Shard and epoch are 0; no reconnect is implemented. The checked uint64 connection sequence starts at 1 and advances for each complete text message, including a nonmarket notice. Every valid recognized market event is written, without exchange-ID filtering. Price/quantity strings remain exact strings inside audit JSON; book values use checked integer scaling by 100000000.

`payload_json` is minified inner JSON, never the outer envelope. CSV fields containing comma, quote, CR or LF are quoted; embedded quotes are doubled. UTF-8 bytes and CRLF records are written in binary mode. One thread parses and writes in order, using buffered output, a one-second flush timer and checked final flush/close. Slow storage delays the event loop; timer scheduling is not a hard persistence guarantee. Write/flush failures are fatal and may leave partial output. No crash-safe or transactional guarantee is claimed.

The development-only validator uses Python's standard-library CSV reader; the application itself is entirely C++:

```sh
python tests/validate_market_csv.py output/market_data_spot_BTCUSDT_<UTC-date>.csv
```

## Limited book policy

The second filename is `market_data_spot_BTCUSDT_<UTC-date>_orderbook.csv`. Both paths must be new. Book rows reuse the triggering receive timestamp, count `seqNo` from 1, use instrument ID 1 and contain integer prices/quantities scaled by 1e8. `P` denotes an applied depth5 replacement, `D` an applied diff. Side is `B` for bid-only diffs, `S` for ask-only diffs, otherwise `N`. Trades never change the book.

A fresh depth5 snapshot anchors sorted bid/ask maps. Diffs set absolute quantities; zero deletes. We retain known levels inside the snapshot's fifth-price boundaries, including known sixth levels. A gap, crossed candidate or unknown fifth-best level clears readiness and waits for a sufficiently fresh snapshot. Stale/covered messages stay in the audit and are counted, without book rows. Short depth5 payloads use zero pairs for missing slots and do not enable subsequent diffs. Every applied event emits a row even if its top five is unchanged.

This is a limited model with an explicit cross-stream update-ID comparability assumption, not Binance's full REST-bootstrap procedure. Equal-revision inconsistencies are fatal. A snapshot that arrives before a matching diff can cover that diff completely. See [design](docs/design.md) for the exact sequence and coverage rules.

## Offline replay

From the project root, after building:

```powershell
.\build-stage4e\binance_capture.exe --replay tests/fixtures/stage4e_market.csv --output-dir build-stage4e/my-replay
```

The output is `build-stage4e/my-replay/stage4e_market_orderbook.csv`. It must not already exist. Replay never creates a Session or TLS context, uses the stored timestamps and event order, and writes no new audit file. A gap in `conn_seq` is allowed; duplicate/backward values within an epoch and decreasing epochs are rejected. New epochs reset the book while output `seqNo` continues. A valid header-only audit produces an empty book file successfully. Malformed input exits 1 with a record number and may leave a valid earlier output prefix. Invalid CLI options exit 2.

Replay accepts the exact nine-column header and current Spot BTCUSDT/shard-0 metadata, minified inner JSON, quoted fields with doubled quotes, LF/CRLF endings and a final record without a newline. Each raw CSV record is limited to 2 MiB + 4096 bytes. Processing uses one record at a time. Policy `limited-top5-v2` adds equal-revision consistency checks for short snapshots; determinism is promised for fixed input and policy, not arbitrary future policy changes.

## Verification

Stage 4E's fresh Windows Release build passed without compiler warnings. The original six tests passed before changes; the expanded suite passed 8/8, including 115 new replay/reliability checks in addition to 120 existing book checks and 25 decimal cases. The 27-event fixture produces 15 independently expected rows (9 `P`, 6 `D`), identically in repeated unit and application-CLI runs. Offline replay of the saved Stage 4D audit regenerated its 580 book rows byte for byte.

AddressSanitizer and UndefinedBehaviorSanitizer probes failed to link because this portable compiler setup lacks `libasan` and `libubsan`. Checked standard-library diagnostic results and all exact build commands are recorded in [Stage 4E verification](docs/stage4e-validation.md). No Stage 4E live-connectivity, injected network shutdown, leak-free, Linux/GCC 12 or performance result is claimed.

Historical live evidence and its all-`P` limitation remain in [Stage 4D verification](docs/stage4d-validation.md). The capture-pair Python validators remain development tools; replay application functionality is C++17. See [design](docs/design.md) for sequence and coverage assumptions. [Assignment requirements](docs/requirements.md) are unchanged.
