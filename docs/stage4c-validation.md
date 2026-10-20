# Stage 4C validation

Scope: inner Binance JSON extraction and the nine-column market CSV only. No order-book reconstruction, book CSV, replay or reconnect implementation.

## Environment and commands

Windows x64, existing isolated MSYS2 UCRT64 toolchain: GCC 16.2.0, CMake 4.4.4, Ninja 1.13.2, Boost 1.92.0, OpenSSL 3.6.5 and nlohmann/json 3.12.0. No additional dependencies downloaded or global environment settings changed. Ubuntu/GCC 12 verification is still required; it was not run here.

Commands executed from the project root, using process-local environment variables:

```powershell
$stageTools = 'C:\Users\hp\.codex\tools\binance-stage4b'
$stagePrefix = "$stageTools\ucrt64"
$env:PATH = "$stagePrefix\bin;$env:PATH"
$env:SSL_CERT_FILE = "$stageTools\ca-bundle.crt"
& "$stagePrefix\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=$stagePrefix/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$stagePrefix/bin/ninja.exe" "-DCMAKE_PREFIX_PATH=$stagePrefix"
& "$stagePrefix\bin\cmake.exe" --build build --parallel 2
& "$stagePrefix\bin\ctest.exe" --test-dir build --output-on-failure
.\build\binance_capture.exe --help
.\build\binance_capture.exe --venue usdm
.\build\binance_capture.exe --venue spot --symbols BTCUSDT --output-dir ./output --duration 60
python tests/validate_market_csv.py output/market_data_spot_BTCUSDT_2026-10-20.csv --session-log build/live-60-stage4c.stderr.log --samples-log build/live-60-stage4c.stdout.log
```

Configure and Release build succeeded without compiler warnings (`-Wall -Wextra -Wpedantic`). All four CTest tests passed: `envelope_validation`, `market_data_csv`, `cli_help`, `cli_rejects_usdm`. After the sandbox profile changed on October 21, a direct CTest invocation failed to launch child processes with loader codes `0xc000007b`/`0xc0000135` because its child environment lacked the portable runtime directory on `PATH`. Running the same CTest command from a short Python subprocess with an explicit *process-local* `env['PATH'] = '<portable-prefix>\\bin;' + env['PATH']` restored that directory and passed all four tests in 0.11 seconds. Direct execution of both test binaries also exited 0. No system PATH or registry settings were changed.

Exact successful CTest rerun under the changed sandbox:

```python
import os
import subprocess
from pathlib import Path

prefix = Path(r'C:\Users\hp\.codex\tools\binance-stage4b\ucrt64')
env = os.environ.copy()
env['PATH'] = str(prefix / 'bin') + ';' + env['PATH']
subprocess.run([str(prefix / 'bin' / 'ctest.exe'), '--test-dir', 'build',
                '--output-on-failure'], env=env, check=True)
```

## Offline checks

- Four recognized stream/control names; eleven malformed/unexpected-envelope cases from Stage 4B, plus mismatching/numeric payload symbols and an unsupported configured symbol.
- Inner object extraction preserves decimal strings, unknown nested fields, a uint64 integer above 2^53, UTF-8, commas, quotation marks, backslashes and escaped control characters. Pretty outer JSON becomes minified inner JSON.
- Independent C++ quoted-field reader checks the exact nine-field header, data-field count, JSON round trip and metadata. Actual CR/LF and empty CSV fields are also exercised separately.
- Timestamp splitting covers a positive timestamp with fractional seconds, an exact second boundary and a negative timestamp. Nanosecond range and zero sequence are rejected by formatting checks.
- Connection sequence starts at 1, increases, permits a nonmarket-message gap and rejects uint64 overflow without wrapping.
- Temporary nested directory/file creation, header visibility, repeated close, refused write/flush after close, existing-file refusal with unchanged bytes, directory-as-file and file-as-directory failures.
- The final executable was invoked against the existing real `./output` capture. It exited 1 with `File exists` before connecting; the CSV SHA-256 was unchanged before and after. This confirms the CLI lifecycle policy on the final binary.
- Actual write to a read-only `FILE` is rejected; the resulting stream error is also reported at flush. This tests the error checks used by the writer. Independent disk-full and close-failure injection were not performed.

A CLI regression exposed an ordering defect: existing-output refusal masked the timer-range error for duration 9223372036854775807. The timer range is now checked before file creation and again before timer scheduling. After rebuilding, **19/19 CLI cases passed**, including help, unsupported venue/symbols, duplicate/unknown/missing options, malformed/zero/overflow durations and the timer-range limit. The maximum `int64_t` duration now exits 2 before touching the existing output file.

## First live capture

The requested `./output` capture exited **0**. Session elapsed time was **60635 ms** including setup/cleanup; the PowerShell stopwatch measured **60798 ms**.

| Kind | CSV rows |
| --- | ---: |
| depth_diff | 590 |
| depth5 | 590 |
| trade | 3180 |
| Total | 4360 |

File: `output/market_data_spot_BTCUSDT_2026-10-20.csv`, **1900809 bytes**. Final connection sequence: **4360**. DNS, TCP, verified TLS and WebSocket all succeeded. Parse, connection and I/O errors were all zero. Three ping frames were observed. Intentional duration shutdown completed with one explicit warning: normal peer WebSocket close code 1000 without TLS `close_notify`.

The independent Python CSV reader validated every row: UTF-8, CRLF, exact header and nine logical fields, integer timestamp ranges, Spot/BTCUSDT/shard 0/epoch 0 metadata, strictly increasing connection sequence, recognized kind, minified inner JSON, and expected payload identifiers. Counts match session counters. All three original printed stream samples match their saved payloads and receive timestamps. Exchange continuity and book semantics were not tested by these checks.

Exact header and first three real CSV rows (as saved, including CSV quote doubling):

```csv
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
1792512926,583198600,spot,depth5,0,0,1,BTCUSDT,"{""lastUpdateId"":101697532027,""bids"":[[""82557.55000000"",""7.69383000""],[""82557.54000000"",""0.94755000""],[""82557.53000000"",""0.00013000""],[""82556.78000000"",""0.00242000""],[""82556.65000000"",""0.00014000""]],""asks"":[[""82557.56000000"",""0.58405000""],[""82557.57000000"",""0.00084000""],[""82557.67000000"",""0.00014000""],[""82558.46000000"",""0.00007000""],[""82559.88000000"",""0.00014000""]]}"
1792512926,597755000,spot,depth_diff,0,0,2,BTCUSDT,"{""e"":""depthUpdate"",""E"":1792512926514,""s"":""BTCUSDT"",""U"":101697531965,""u"":101697532027,""b"":[[""82557.55000000"",""7.69383000""],[""82555.72000000"",""0.00014000""],[""82555.71000000"",""0.08489000""],[""82555.70000000"",""0.24567000""],[""82554.42000000"",""0.00614000""],[""82554.41000000"",""0.65507000""],[""82554.40000000"",""1.34434000""],[""82554.39000000"",""0.00000000""],[""82554.38000000"",""1.90228000""],[""82553.36000000"",""0.00200000""],[""82548.59000000"",""0.94728000""],[""82548.08000000"",""0.03075000""],[""82547.51000000"",""0.02432000""],[""82543.70000000"",""0.10240000""],[""82525.49000000"",""1.82052000""],[""82445.47000000"",""0.03759000""],[""82354.24000000"",""0.00756000""],[""82326.23000000"",""0.00000000""],[""81628.69000000"",""0.03965000""],[""81356.67000000"",""0.04284000""],[""81319.19000000"",""0.00008000""],[""81191.82000000"",""0.03684000""],[""80779.67000000"",""0.03696000""],[""80367.53000000"",""0.03720000""],[""79955.39000000"",""0.03744000""],[""74301.80000000"",""0.00039000""]],""a"":[[""82557.56000000"",""0.58405000""],[""82557.57000000"",""0.00084000""],[""82560.25000000"",""0.00025000""],[""82565.10000000"",""0.04007000""],[""82565.73000000"",""0.00000000""],[""82565.74000000"",""0.00027000""],[""82565.81000000"",""0.00015000""],[""82567.16000000"",""0.00000000""],[""82570.58000000"",""0.21541000""],[""82601.64000000"",""0.00000000""],[""82638.61000000"",""0.00000000""],[""82649.28000000"",""0.12730000""],[""82689.95000000"",""0.00000000""]]}"
1792512926,682176400,spot,depth5,0,0,3,BTCUSDT,"{""lastUpdateId"":101697532057,""bids"":[[""82557.55000000"",""6.86934000""],[""82557.54000000"",""0.94755000""],[""82557.53000000"",""0.00013000""],[""82556.78000000"",""0.00242000""],[""82556.65000000"",""0.00014000""]],""asks"":[[""82557.56000000"",""0.39172000""],[""82557.57000000"",""0.00084000""],[""82557.67000000"",""0.00014000""],[""82558.46000000"",""0.00007000""],[""82559.88000000"",""0.00014000""]]}"
```

The first validator attempt could not decode PowerShell's UTF-16 redirected logs. The validator was corrected to detect a log BOM; the application CSV remained UTF-8 and was unchanged. The complete validation then passed.

This live run preceded the small timer-range preflight correction described above. That correction does not change message parsing, CSV formatting or WebSocket processing. The final rebuilt executable was tested with CTest and the 19-case CLI regression; no second 60-second network run is claimed.

Raw logs: `build/live-60-stage4c.stdout.log`, `build/live-60-stage4c.stderr.log`, `build/live-60-stage4c.result.txt` (local ignored build artifacts). Windows PowerShell displays stderr diagnostics as `NativeCommandError` records when redirected; the separately recorded application exit code was 0.

## Files and ownership

- Added `src/market_data.hpp` / `.cpp`: receive metadata, timestamp/sequence helpers, CSV field/row formatting and RAII file ownership. `_wopen`/`open` exclusively creates the file, then buffered binary `FILE` I/O writes it. Header flush, one-second flush timer and checked final close expose storage errors. Normal close is checked; the destructor provides fallback resource cleanup.
- Changed `src/envelope.hpp` / `.cpp`: one `ordered_json` parse returns kind, configured symbol and minified inner payload. A supplied `s` must match the subscription. No price-level validation or conversion yet.
- Changed `src/session.hpp` / `.cpp`: moved timestamp helpers into the CSV metadata module, assign checked text-message sequences, write before per-kind counters, schedule flushing, finalize after handlers drain and include row/I/O counters. Networking and TLS shutdown logic are retained.
- Changed `src/main.cpp`, `src/config.cpp`: remove the obsolete unused-output notice and update help. Preserve argument validation and exit conventions.
- Changed `CMakeLists.txt`, `tests/envelope_tests.cpp`; added `tests/market_data_tests.cpp` and development-only `tests/validate_market_csv.py`.
- Updated `README.md`, `docs/design.md`; added this verification record. `docs/requirements.md` is unchanged (SHA256 `3428EF37F2A671831A5F95661038E04A81733368A7C1502F5D64DFE1D1DDCB83`).

## Stage 4D interface and limits

`MarketEvent` owns the event kind, symbol and inner JSON string. `EventMetadata` carries the original receive time and connection sequence. Stage 4D should use these same metadata after successful audit writing, add full payload/decimal/sequence validation and book processing, and introduce the separate 26-column writer. To avoid reparsing when typed book decoding is added, retain/use the already parsed inner DOM within envelope processing at that stage. This stage creates no book state or second CSV.

Epoch is fixed at 0 because reconnect is not supported. Exchange IDs are preserved without interpretation. Wall-clock values are not sorted or forced monotonic; nanosecond units do not promise nanosecond clock resolution. One event-loop thread performs buffered file writes; slow storage can delay reads, timers and shutdown. Flush is not fsync/crash durability, and partial files are possible after failures. Existing date-based files are refused; use a different output directory for a repeat capture. No Linux build, sanitizer run, injected network failure, disk-full or close-failure test is claimed for this stage.
