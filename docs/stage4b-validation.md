# Stage 4B verification record

This records actual Windows results, not final Linux compatibility or order-book correctness. Stage 4B implements only secure message reception, envelope inspection, timestamps, samples, counters and shutdown.

## Toolchain and provenance

Host: Windows 11 x64, build 26300. WSL reported it was not installed. The old MinGW GCC 6.3.0 was not used for this build.

With explicit user approval, packages targeting **x86_64 UCRT64** were extracted under `C:\Users\hp\.codex\tools\binance-stage4b`, outside the repository, from the [official MSYS2 repository](https://repo.msys2.org/mingw/ucrt64/).

| Tool/dependency | Package version | Actual version/discovery |
| --- | --- | --- |
| GCC | 16.2.0-6 | 16.2.0; target `x86_64-w64-mingw32` |
| CMake | 4.4.4-3 | 4.4.4 |
| Ninja | 1.13.2-1 | 1.13.2 |
| Boost headers | 1.92.0-4 | Boost-1.92.0 and boost_headers-1.92.0 selected |
| OpenSSL | 3.6.5-1 | 3.6.5 |
| nlohmann/json | 3.12.0-1 | CMake found 3.12.0 |

There were 53 archives, approximately 117 MiB, including compiler/CMake runtime dependencies. No complete MSYS2 installation or unused compiled Boost library package was installed. Archive SHA-256 hashes matched the official repository metadata fetched over HTTPS. `installed-manifest.json` in the isolated tools directory records every URL/version/hash. No package installation hooks were executed.

The extracted CA package had an empty generated bundle. A nonempty bundle from the existing Git for Windows installation was copied to the isolated directory and selected with process-local `SSL_CERT_FILE`; `ca-source.json` records its source and hash. Certificate-chain and hostname verification stayed enabled. No system PATH, registry, system trust store or global environment settings were changed.

## Actual build and test commands

From the project root, using process-local environment settings:

```powershell
$stageTools = 'C:\Users\hp\.codex\tools\binance-stage4b'
$stagePrefix = "$stageTools\ucrt64"
$env:PATH = "$stagePrefix\bin;$env:PATH"
$env:SSL_CERT_FILE = "$stageTools\ca-bundle.crt"
& "$stagePrefix\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=$stagePrefix/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$stagePrefix/bin/ninja.exe" "-DCMAKE_PREFIX_PATH=$stagePrefix"
& "$stagePrefix\bin\cmake.exe" --build build --parallel 2
& "$stagePrefix\bin\ctest.exe" --test-dir build --output-on-failure
```

Configure and Release compilation/linking succeeded with C++17, extensions disabled, and `-Wall -Wextra -Wpedantic`. No compiler warnings were emitted. CMake reran automatically after test-target additions. All **3/3 CTest tests passed**: `envelope_validation`, `cli_help`, `cli_rejects_usdm`. The envelope executable checks 4 stream categories, including the control notice, and 11 invalid/unexpected envelopes.

Twenty additional CLI invocations were checked through a temporary Python subprocess harness: help, short help, valid options with help, unsupported venue/symbol/list, unknown option, missing value, option replacing value, empty path, duplicate option, zero/negative/fractional/exponent duration, integer overflow, timer overflow, help plus invalid option, equals syntax and positional argument. Exact exit-code/message checks passed. Python is a development harness, not an application runtime dependency or replay implementation.

## Final real 30-second capture

The final executable was invoked with:

```powershell
.\build\binance_capture.exe --venue spot --symbols BTCUSDT --output-dir ./output --duration 30
```

A subprocess harness launched that command with the same child environment, measured elapsed time and saved intact stdout/stderr under the ignored build directory. The recorded application summary was:

```text
Summary: DNS=1 TCP=1 TLS=1 WebSocket=1
Messages=2927 depth_diff=276 depth5=276 trade=2375
Parse errors=0 connection errors=0 TLS shutdown warnings=1 ping frames=1 elapsed_ms=32041 stop_reason=duration expired
```

Exit code **0**. Outer process measurement: **32.430 seconds**, including startup/cleanup overhead. The duration timer covers the 30-second session including setup; cleanup then completed within its five-second deadline. All three stream counts are positive and sum to the complete-message count. One server ping was observed.

The peer sent a normal WebSocket close code 1000 but omitted TLS `close_notify`. This is explicitly logged and counted as a TLS shutdown warning only during intentional shutdown after that confirmed normal close. Truncated reads during capture and unconfirmed closes remain errors. [Beast's TLS shutdown discussion](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_io/ssl_tls_shutdown.html) explains why complete framed messages differ from using transport EOF to infer message completion.

Full samples and summary are in `build/live-30-final.stdout.log` and `build/live-30-final.stderr.log`. The executable prints one full original sample per required stream, then counters. It does not abbreviate payloads. No output directory or CSV was created.

Earlier attempts are retained in the work history: an empty CA bundle failed TLS verification before any message. After selecting a valid bundle, the initial 30-second run received 3425 messages (289/289/2847) but exited 1 because the original cleanup policy treated missing TLS close_notify as fatal. The narrow normal-close warning policy was implemented, rebuilt and verified by the final run above.

## Real sample excerpts

Only these explanations abbreviate payloads. Ellipses are illustrative, not literal valid JSON; source logs contain complete messages.

```text
Trade:
{"stream":"btcusdt@trade","data":{"e":"trade","E":1792507555614,"s":"BTCUSDT","t":6777888577,"p":"83108.00000000","q":"0.00010000","T":1792507555613,"m":true,"M":true}}

depth5:
{"stream":"btcusdt@depth5@100ms","data":{"lastUpdateId":101690437819,"bids":[["83108.00000000","1.75780000"],...],"asks":[["83108.01000000","6.34043000"],...]}}

Diff:
{"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","E":1792507555614,"s":"BTCUSDT","U":101690437643,"u":101690437819,"b":[["83108.00000000","1.75780000"],...],"a":[["83108.01000000","6.34043000"],...]}}
```

The first trade's local receive timestamp was `1792507555` seconds plus `694546100` nanoseconds. It is independent of the payload's exchange times. Matching update IDs in these excerpts are observations only; envelope reception does not validate the proposed reconstruction algorithm.

## Failure and signal checks

- Selecting an empty CA file for a real `--duration 5` invocation allowed DNS/TCP but failed TLS with `certificate verify failed`. Exit 1, zero messages, zero parsing errors, one connection error. Verification was not bypassed.
- Windows Ctrl+Break sent while connecting stopped the program with `stop_reason=signal 21`, exit 1 and zero messages. This is correctly an incomplete capture. An initial test-harness expectation of exit 0 was corrected after observing that legitimate behavior.
- Another run waited for one sample from every stream, then received Windows Ctrl+Break through Python `send_signal(CTRL_BREAK_EVENT)` to a new process group. Exit 0, `stop_reason=signal 21`, 151 messages (1 diff, 1 depth5, 149 trades), zero parse/connection errors, one normal-close TLS warning, session elapsed 2438 ms. This exercised the shared signal shutdown handler after successful reception. Interactive Ctrl+C itself was not manually pressed; SIGINT and SIGTERM are registered through Asio.

DNS failure, TCP refusal, abrupt post-handshake disconnect, stalled-close deadline and deliberate hostname-mismatch cases have implementation error paths but were not individually injected. Invalid envelopes were tested directly, not injected into Binance. No sanitizer/leak-check or Linux build result is claimed.

## Stage boundary

The application uses one event loop and one connection with no worker threads. RAII owners survive until cancelled handlers drain; socket shutdown/close is the fallback on fatal error or timeout. Automatic reconnect/resynchronization is deferred. CSV output, price/quantity conversion, full event decoding, order-book reconstruction and replay are not implemented. Final Linux/GCC 12 build and runtime verification remain required.
