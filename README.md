# Binance market-data capture

Stage 4C captures Binance Spot BTCUSDT depth, depth5 and trade events over one verified TLS WebSocket into the assignment's nine-column market CSV. It also prints one complete sample per stream and reports counters. The order book, second CSV, replay and automatic reconnect are not implemented.

## Linux build

Requirements: C++17 (GCC 12 recommended), CMake 3.16+, Boost headers 1.74+, OpenSSL development files 1.1.1+ (supported OpenSSL 3 recommended), and nlohmann/json 3.10+ with its CMake package. CMake discovers local dependencies; it does not download them.

These Ubuntu commands are provided for final Linux verification and have not been run in the current Windows environment:

```sh
sudo apt-get update
sudo apt-get install -y g++-12 cmake ninja-build libboost-dev libssl-dev nlohmann-json3-dev ca-certificates
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-12
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
./build-linux/binance_capture --venue spot --symbols BTCUSDT --output-dir ./output --duration 60
```

## Tested portable Windows build

The isolated toolchain is outside this repository at `C:\Users\hp\.codex\tools\binance-stage4b`. GCC 16.2.0, CMake 4.4.4, Ninja 1.13.2, Boost 1.92.0, OpenSSL 3.6.5 and JSON 3.12.0 were used. Official MSYS2 UCRT64 packages and required runtime dependencies were downloaded with user approval and archive SHA-256 verification. `installed-manifest.json` in that folder records versions, sources and hashes. No system PATH, registry or global environment settings were modified.

From the project root in PowerShell (environment changes affect only that process and its children):

```powershell
$stageTools = 'C:\Users\hp\.codex\tools\binance-stage4b'
$stagePrefix = "$stageTools\ucrt64"
$env:PATH = "$stagePrefix\bin;$env:PATH"
$env:SSL_CERT_FILE = "$stageTools\ca-bundle.crt"
& "$stagePrefix\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=$stagePrefix/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$stagePrefix/bin/ninja.exe" "-DCMAKE_PREFIX_PATH=$stagePrefix"
& "$stagePrefix\bin\cmake.exe" --build build --parallel 2
& "$stagePrefix\bin\ctest.exe" --test-dir build --output-on-failure
.\build\binance_capture.exe --venue spot --symbols BTCUSDT --output-dir ./output --duration 60
```

The extracted CA package's generated bundle was empty without its installation hook. A nonempty CA bundle from the existing Git for Windows installation was copied to `$stageTools\ca-bundle.crt`; `ca-source.json` records its source and hash. Certificate-chain and hostname verification remain enabled. Linux uses its installed OpenSSL CA roots by default; `SSL_CERT_FILE` may select an explicitly trusted PEM bundle where needed.

## CLI and behavior

| Option | Behavior |
| --- | --- |
| `--help`, `-h` | Show usage; exit 0. |
| `--venue spot` | Default `spot`; other venues rejected. |
| `--symbols BTCUSDT` | Default `BTCUSDT`; other symbols/lists rejected. |
| `--output-dir PATH` | Default `./output`; create directories, refuse an existing capture file. |
| `--duration SECONDS` | Positive integer within timer range; includes connection setup. Omit to run until interrupted or an error occurs. |

Use separate option/value arguments. Unknown/duplicate options, missing values and invalid durations exit 2. All arguments are checked even with `--help`. Connection, envelope, file and incomplete-capture failures exit 1. A duration/signal stop exits 0 only after all three streams were observed without a fatal error and the CSV was finalized. No reconnect or insecure TLS fallback exists.

SIGINT/Ctrl+C and SIGTERM request orderly close; Windows Ctrl+Break is also supported. Cleanup waits at most five seconds for WebSocket/TLS close. Beast handles ping/pong and fragmentation. Missing TLS `close_notify` is an explicit warning only after a normal peer WebSocket close code 1000 during an intentional stop. Truncated live reads and unconfirmed closes remain failures.

The endpoint is exactly `wss://stream.binance.com:9443/stream?streams=btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade`. Parse once, validate the exact stream and object-valued `data`, and check `s` against BTCUSDT when supplied. Depth5's symbol comes from its subscription. Preserve all inner fields and decimal strings; quantities and exchange sequencing are not interpreted. Samples remain intact. Messages received solely for the closing handshake after stop are outside capture counters.

## Market CSV policy

The filename is `market_data_spot_BTCUSDT_<UTC-date>.csv`, using the UTC run-start date, retained across midnight. Existing files are refused without modification; choose another output directory for another run on the same date. A failed connection may leave a header-only file.

```text
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
```

Capture local wall-clock time immediately after a complete read, before parsing. Split it into integer seconds and a nonnegative nanosecond remainder; clock resolution depends on the OS. Physical row order is processing order, even if the clock moves backward. Duration and flush timers use a monotonic clock. Shard and epoch are 0; no reconnect is implemented. The checked uint64 connection sequence starts at 1 and advances for each complete text message, including a nonmarket notice. Every valid recognized market event is written, without exchange-ID filtering. No fixed-point conversion occurs in this stage: price/quantity strings remain exact strings inside JSON.

`payload_json` is minified inner JSON, never the outer envelope. CSV fields containing comma, quote, CR or LF are quoted; embedded quotes are doubled. UTF-8 bytes and CRLF records are written in binary mode. One thread parses and writes in order, using buffered output, a one-second flush timer and checked final flush/close. Slow storage delays the event loop; timer scheduling is not a hard persistence guarantee. Write/flush failures are fatal and may leave partial output. No crash-safe or transactional guarantee is claimed.

The development-only validator uses Python's standard-library CSV reader; the application itself is entirely C++:

```sh
python tests/validate_market_csv.py output/market_data_spot_BTCUSDT_<UTC-date>.csv
```

## Verification

The Stage 4C Windows Release build completed without compiler warnings and all 4 CTest tests passed. Offline tests exercise envelope classification, malformed/symbol errors, inner JSON preservation, CSV round trips, metadata/timestamps, sequence behavior, exclusive file creation and file errors. Actual live counts and all-row validation are in the Stage 4C record below.

See [Stage 4C verification](docs/stage4c-validation.md) and the historical [Stage 4B record](docs/stage4b-validation.md). Windows testing does not replace final Linux/GCC 12 verification. The reconstruction algorithm in [the working design](docs/design.md) remains unimplemented and unverified. [Assignment requirements](docs/requirements.md) are unchanged.
