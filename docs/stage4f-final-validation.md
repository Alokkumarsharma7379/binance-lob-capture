# Final validation and submission preparation

Date: October 21, 2026. Starting/current checkpoint: `31165a94722b790626a66d8328b590baa1077600` on `main`, matching the local `origin/main` reference. Phases 3–6 prepared the repository for user review; no commit, push, tag or release was performed.

## Outcome

| Check | Actual result |
| --- | --- |
| Clean Windows C++17 Release configure/build | Exit 0; no compiler warnings. |
| Windows CTest | 8 passed, 0 failed; exit 0; reported time 1.10 seconds. |
| Clean Linux C++17 Release configure/build | Exit 0; no compiler warnings; no portability changes required. |
| Linux CTest | 8 passed, 0 failed; exit 0; reported time 0.72 seconds. |
| Existing full live capture | Both all-row CSV validators passed again, exit 0. Source hashes unchanged. |
| Live reconciliation audit | All 590 diffs match preceding snapshot final IDs; 443 nonempty in-range comparisons agree; no contradictions or diff-sequence gaps. |
| Genuine submission excerpt | 225 consecutive market rows and all 20 corresponding book rows, unchanged byte prefixes. |
| Windows sample replay | Exit 0; byte-identical to the genuine paired book excerpt. |
| Linux sample and full-capture replay | Both exit 0; `cmp` confirms byte-identical book output against Windows originals. |
| Repository candidate-file audit | All 25 C++ source/header files already tracked; no build/toolchain artifacts or credential-pattern findings. Nothing staged. |
| Remaining transport issue | The failed 120-second run is explained by the configured idle-timeout termination; the cause of peer-traffic silence remains unresolved. |

No application code, CMake target or test fixture was modified. The final changes are README, this report, genuine sample files/provenance, explicit sample ignore/attribute rules and the optional editor compilation-database path. The existing editor-path modification was retained in intent and aligned with `build-windows`.

## References and schema audit

Reviewed the original six-page assignment extraction previously checked against the PDF, `docs/requirements.md`, `docs/design.md`, `docs/stage4e-validation.md`, `docs/stage4f-phase1-2.md`, and the actual current source. Source review included envelope/depth decoding, processing order, decimal conversion, maps/sequencing, CSV formatting, replay and network lifecycle. Requirements and the PDF remain unchanged.

The market header matches the PDF's fixed column table, in this order:

```csv
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
```

The book header matches the complete column-semantics table and 26-column requirement. The PDF's printed header is clipped, as already disclosed in the requirements document:

```csv
tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,ask0,ask1,ask2,ask3,ask4,ask_size0,ask_size1,ask_size2,ask_size3,ask_size4
```

Both outputs retain UTF-8, CRLF and CSV quoting. The audit holds minified inner objects, with quotes doubled for the CSV field. CSV-aware validation read every actual record rather than counting raw commas. Timestamps use signed int64 seconds and int32 nanosecond remainders; connection/book sequences are uint64, instrument ID is 1, event letters are `P/D`, and side letters `B/S/N`. Prices/quantities use checked int64 scaling by 100000000. Book sorting, pairing of sizes, integer bounds, metadata, receive order, timestamps and exact payload correspondence passed the existing validators.

Audit-first processing remains intact: all recognized market events are recorded before book mutation. Trades, covered diffs and rejected candidates do not emit book rows; every applied depth event does. No synthetic trade mutation or fabricated `D` row was introduced. The conditional limited-book interpretation remains documented rather than being represented as universal assignment acceptance.

The rechecked full file is the successful Windows 60-second capture under `output/stage4f-60-20261021T093503Z/`, not either header-only failure or the failed longer-run prefix. It has 2,431 market rows (590 depth5, 590 diffs, 1,251 trades), 590 book rows, `conn_seq` through 2,431 and book sequence through 590. The completed process exited 0 after 60,403 ms with zero fatal errors and one reported TLS shutdown warning. No new live capture was necessary in Phases 3–6.

## Why every live book row is P

Independent audit logic walked the saved market rows in receive order and compared each diff to the preceding depth5 payload. Every one of the 590 diff final IDs `u` equals that preceding snapshot's `lastUpdateId`. They are already represented in the accepted book state, so the documented `u <= L` rule counts them as covered and emits no duplicate book row.

There were 443 diffs with at least one changed price inside the snapshot's known ranges. Independently parsed decimal quantities at all such prices matched the snapshot. The remaining 147 diffs offered no in-range comparison; they are not counted as quantity confirmations. Diff-to-diff continuity had no gaps. The result agrees with the program's 590 covered diffs, 590 applied snapshots and zero gap/wait/coverage-loss/crossing events.

Uncovered diffs are supported, not disabled: the existing hand-calculated 27-event fixture produces 15 expected rows, including **six D rows** and nine P rows. It exercises overlapping/contiguous updates, zero deletion, duplicates, stale snapshots, gaps, recovery, repeated refreshes and epoch changes. Both clean builds ran that fixture and the existing 120 book/pipeline and 115 replay/reliability checks. Additional copies of the same tests were unnecessary.

This live file does not exercise applied D output; fixture success does not prove every exchange cross-stream guarantee. Snapshot/diff comparability and the approved coverage-based drop policy remain explicit assumptions. Full REST bootstrap is absent. The algorithm can promote a retained known sixth level, but clears readiness when a genuinely unknown level would be needed. No order-book redesign was made during final preparation.

## Long-run timeout and TLS warning

The failed 120-second attempt's saved audit spans **49.0032312 seconds** between first and last complete market events. Its final message count then repeats while five-second reporting continues. Only two server pings were counted. It exits after **110,093 ms** with the WebSocket read timeout; duration expiration is not logged.

Source sets `websocket::stream_base::timeout{15s, 60s, false}`, disables the underlying TCP timeout after TLS and keeps an asynchronous read outstanding. The termination is therefore consistent with the configured 60-second WebSocket idle policy after peer traffic stopped, not the requested 120-second duration. [Beast documents](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket/timeouts.html) that the idle limit closes a stream without incoming data/control frames and reports the error through an outstanding read. `false` disables unsolicited client keepalive pings; automatic replies to server pings are still handled by Beast's read operation.

Continued timer reports show the event loop was responsive during the silence, so a permanently blocked consumer is not supported by this record. It does not identify whether Binance, a local network path or an intermediary stopped delivery. There is no packet-level evidence to select one. Blindly extending the timeout or rewriting networking would hide rather than establish a fix. The timeout remains unresolved and a stable 120-/300-second run is not claimed. Automatic reconnect remains unimplemented; the failure correctly exits nonzero and retains its recorded prefix.

The successful run's shutdown warning is separate: during intentional stop, the peer completed a normal WebSocket close (1000) but omitted TLS `close_notify`. The source reports that narrow case and keeps truncated live reads or unconfirmed closes fatal. [Beast's TLS shutdown guidance](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_io/ssl_tls_shutdown.html) distinguishes missing shutdown alerts from completion of independently framed application messages. No demonstrably necessary small application fix was found, so the warning was neither hidden nor broadly ignored. Certificate-chain/hostname verification, SNI and TLS minimums are unchanged.

The Windows trust correction from Phases 1–2 remains explicit: select the verified existing 150-certificate PEM bundle using `SSL_CERT_FILE`. Empty portable default CA files were not silently repaired or shipped. Linux build/replay tests do not establish live Linux TLS connectivity.

## Linux environment and actual commands

WSL inspection now found Ubuntu 26.04.1 LTS on WSL2, x86_64. It initially lacked development tools. After identifying packages/sources to the user, the minimal dependencies were installed from the configured official Ubuntu repositories. No Windows global settings were changed.

Installed/tested versions:

| Component | Linux result |
| --- | --- |
| Compiler | GCC 15.2.0, Ubuntu build `15.2.0-16ubuntu1` |
| CMake | 4.2.3 |
| Build tool | GNU Make 4.4.1 |
| Boost headers | 1.90.0 (`libboost-dev` package `1.90.0.1ubuntu3`) |
| OpenSSL | 3.5.5 (`libssl-dev` package `3.5.5-1ubuntu3.7`) |
| nlohmann/json | Library 3.11.3; Ubuntu's package version has a longer `3.12.0.really...3.11.3` name. |

GCC 12 and Ninja were not available from the configured repository set. No new repository was enabled to obtain them. GNU Make and GCC 15 satisfy this phase's common Linux/C++17 build requirement; GCC 12 and Clang remain untested.

Actual WSL dependency commands:

```sh
apt-get update
apt-get install -y --no-install-recommends g++ cmake make libboost-dev libssl-dev nlohmann-json3-dev
```

The WSL user was root, so `sudo` was not needed; README includes it for a normal Ubuntu user. `ca-certificates` was already installed. Build/test commands, from the actual repository mounted under `/mnt/c/Users/hp/OneDrive/Desktop/Coading/binance-lob-capture`:

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DBUILD_TESTING=ON
cmake --build build-linux --parallel 2
ctest --test-dir build-linux -V --output-on-failure
./build-linux/binance_capture --replay samples/market_data_spot_BTCUSDT_2026-10-21.csv --output-dir ./output/sample-replay
cmp samples/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv output/sample-replay/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv
./build-linux/binance_capture --replay output/stage4f-60-20261021T093503Z/market_data_spot_BTCUSDT_2026-10-21.csv --output-dir ./output/final-linux-full-replay
cmp output/stage4f-60-20261021T093503Z/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv output/final-linux-full-replay/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv
```

All configure/build/test/replay/comparison commands succeeded. The build directory was new. Linux used the same source, with no portability patches. The sample book SHA-256 is `eb3c3698757dae1c3253121de271497e2ba715e46cde2f211d268ebec15864c8`; full book SHA-256 is `fd1723ffbde87d1b032b581aa9465f8660766bb9a2e2ea243fafd64ac921d068`, equal to their Windows originals. No Linux live run, sanitizer run or performance result is claimed.

## Windows commands and README verification

A new `build-windows` directory was configured using the existing authorized UCRT64 installation (GCC 16.2.0, CMake 4.4.4, Ninja 1.13.2, Boost 1.92.0, OpenSSL 3.6.5, JSON 3.12.0). These are the README's commands, with the prefix set to the existing installation:

```powershell
$capturePrefix = "$env:USERPROFILE\.codex\tools\binance-stage4b\ucrt64"
$env:PATH = "$capturePrefix\bin;$env:PATH"
& "$capturePrefix\bin\cmake.exe" -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DCMAKE_CXX_COMPILER=$capturePrefix/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$capturePrefix/bin/ninja.exe" "-DCMAKE_PREFIX_PATH=$capturePrefix"
& "$capturePrefix\bin\cmake.exe" --build build-windows --parallel 2
& "$capturePrefix\bin\ctest.exe" --test-dir build-windows -V --output-on-failure
```

The actual launcher used Python subprocess argument arrays and a child-only PATH. Configure, all 48 clean build steps and CTest succeeded. Compilation/configuration logs contain no compiler/CMake warnings. Windows sample replay also exited 0 and matched the paired file bytes. Existing CSV validators were executed against the full capture with the actual session/sample logs and against the submission market excerpt. The live command/TLS behavior had already been executed in Phases 1–2; it was not needlessly repeated just to edit documentation.

README now states supported scope, dependencies, real tested versions, build/live/replay commands, both exact headers, scales/timestamps/IDs, known-range algorithm, sequences/epochs/reconnect behavior, threading/I/O, trust configuration, tests, genuine samples, unresolved timeout and submission steps. Commands requiring a fresh directory say so. Historical validation documents keep clearly identified machine-local evidence paths; README uses a user-relative example prefix. It does not claim GCC 12, Clang, Linux live capture, full-depth sync or successful 300-second testing.

## Genuine sample provenance

Prepared two explicitly allowed CSVs:

- `samples/market_data_spot_BTCUSDT_2026-10-21.csv`: 225 rows, 98,489 bytes, sequences 1–225; 20 depth5, 20 diffs, 185 trades.
- `samples/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv`: all corresponding first 20 book rows, 5,002 bytes, sequence 1–20.
- `samples/README.md`: interval, full-run result, source hashes, excerpt hashes and replay instructions.

Both CSVs are unchanged byte prefixes of the successful run. Selection stops at the first market row at least two seconds after the first receive time; no event inside the prefix is removed. The actual excerpt span is 1.9610324 seconds. All three stream kinds are present. The excerpt is labeled as such, not passed off as a complete minute. Its paired book file includes every output triggered in that interval and is byte-identically replayable on Windows and Linux.

The original full capture hashes are unchanged: market `f0f1a2084cad1ec3d567656729f68ca7bdf2a04ab908162a2bc1ed5b2678f4cc`, book `fd1723ffbde87d1b032b581aa9465f8660766bb9a2e2ea243fafd64ac921d068`. The market excerpt hash is `21ad55da9361bbcf0596fd6a32aea142c9dd038e9b5f46c3d25b8515df168dac`. No synthetic data or doctored D rows were substituted. The separate synthetic regression fixture remains clearly identified under `tests/fixtures`.

`.gitignore` permits only the two named sample CSVs in addition to existing fixtures; large captures remain ignored. `.gitattributes` preserves sample CRLF bytes across checkouts. GitHub can display their CSV text; these attributes disable newline normalization, not text diffs.

## Repository audit and files ready for review

All 25 files under `src/` are already tracked in the checkpoint, along with CMake, tests, replay fixtures, requirements, design and historical records. No source/test/CMake/requirements/PDF diff was introduced in this pass. Candidate-file inspection found no compiler binaries, libraries, build directories, archives or large generated captures. Literal private-key/token/credential-assignment patterns found no credentials. Harmless documentation references to secrets are not treated as credentials. No private configuration was read or copied into the repository.

The sample exceptions work; `build-linux`/`build-windows` and output directories remain ignored. Nothing is staged. HEAD and the local `origin/main` reference remain `31165a9`. The `v1.0.0` tag does not exist locally. No network push or publication was performed.

Final files to review/commit:

```text
.gitattributes
.gitignore
.vscode/c_cpp_properties.json
README.md
docs/stage4f-final-validation.md
samples/README.md
samples/market_data_spot_BTCUSDT_2026-10-21.csv
samples/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv
```

After explicit user approval, suggested commands (not executed here):

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

Before those commands, review the entire pending diff and confirm no unrelated changes arrived. The repository URL is `https://github.com/Alokkumarsharma7379/binance-lob-capture`. Samples become accessible to remote reviewers only after the approved commit/push; preparation alone does not publish them.

## Evidence and remaining limits

Local ignored evidence directory: `build-stage4f-final-20261021T101325Z/`. It contains Linux inventory/dependency/build/version/replay logs, Windows clean-build/CTest logs, full-capture CSV audit logs, reconciliation and timeout analyses, sample provenance/replay results, repository audit and final preservation checks. No toolchain or generated build product is proposed for Git.

Submission preparation is complete for review, with explicit limitations: unresolved traffic silence in the longer run; no stable 120-/300-second capture claim; live D application covered by fixtures rather than the saved live run; limited-range cross-stream synchronization assumptions; no automatic reconnect/REST full sync; GCC 12/Clang and Linux live TLS untested; no sanitizer/leak-freedom or performance claim; buffered nontransactional output and one-thread back-pressure. These are not silently marked as passed acceptance criteria.

No further implementation is pending within Phases 3–6. Commit, push, tag and release remain blocked on the user's explicit approval, as requested.
