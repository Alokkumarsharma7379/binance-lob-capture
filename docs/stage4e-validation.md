# Stage 4E: reliability and offline replay

Validated on October 21, 2026, using the existing isolated Windows x64 toolchain. A fresh Release build and all eight CTest tests passed. Replay is implemented in C++17 and was verified against independently calculated fixture output and the saved Stage 4D book file. No live capture, TLS troubleshooting, endpoint change, Linux validation, commit, push or tag was attempted.

The user reports `TLS handshake failed: certificate verify failed` as a current live blocker. That issue remains for Stage 4F; no Stage 4E result establishes current Binance connectivity. Historical Stage 4D capture evidence remains historical.

## Starting state and references

HEAD remained `eca42cbcdf2a4c383e615f819f75962263d8d3f9`. Stage 4D source, tests and documentation were present as tracked modifications and untracked files; they were preserved. The six existing offline tests were run before changes: 6 passed, 0 failed, CTest exit 0. Baseline output is `build-stage4d/stage4e-baseline-tests.log`.

References were the existing source/tests, `docs/design.md`, `docs/requirements.md`, and the previously reviewed PDF extraction, including its reconnect/epoch, no-network grading replay, deterministic output, ownership and shutdown requirements. Requirements and the PDF remain byte-for-byte equal to HEAD. No tracked files were deleted. The requirements SHA-256 remains `3428ef37f2a671831a5f95661038e04a81733368a7c1502f5d64dfe1d1ddcb83`.

Existing behavior already included checked decimals, absolute depth quantities, zero deletion, sorted maps, snapshot replacement, stale/equal/overlapping sequence handling, gap/coverage recovery, epoch reset, audit-before-mutation, exclusive output files, buffered I/O with checked flush/close, duration/signals, connection deadlines and a bounded WebSocket message size. These components were reused.

## Changes and findings

| Finding or need | Change | Evidence |
| --- | --- | --- |
| A short snapshot had valid known values but `ready == false`; equal-ID comparison was gated on readiness and could silently accept contradictory values. | Track valid stored state separately from readiness; compare equal-revision snapshots even when short. Contradiction invalidates and fails. | Regression checks consistent short refresh and contradictory short refresh with independently specified quantities. |
| Shared processing could leave readiness intact after a decoder or book-output exception. | Extract `process_book_event`, invalidate on exceptions, then rethrow. The live wrapper still writes the audit first. | Start with a ready book, inject a malformed depth event, verify readiness clears and book rows remain unchanged. |
| Session ended a failed connection with its last book still marked ready in memory until destruction. | `Session::finish()` explicitly invalidates before socket teardown. | Source-reviewed lifecycle change; a new real/injected transport failure was not executed in this stage. |
| Replay was absent. | Add bounded quoted-field CSV reading, metadata validation and a network-free replay branch sharing decoder/book/output logic. | Independent 27-event fixture, repeated CLI runs and offline regeneration of saved Stage 4D output. |
| Retained-level limit needed direct coverage. | Add two valid 10,000-change batches that exceed the 20,000 retained-level bound. | First batch applies; second throws and leaves the book invalid. |
| Byte comparisons depend on fixture line endings surviving checkout. | Add exact `.gitattributes` entries marking the two CSV fixtures binary for Git normalization purposes. | `git check-attr text` reports `unset`; both fixtures contain CRLF only. |

No fundamental redesign of the limited-book algorithm was needed. Policy identifier is now `limited-top5-v2` because the short-snapshot consistency fix intentionally changes treatment of an invalid old tape. Fixed input and fixed policy produce deterministic bytes; compatibility with every historical policy is not promised.

## Files added or modified in this stage

Added:

- `src/csv_reader.hpp/.cpp`: one-record CSV parser with quote, EOF, read-error and byte/field bounds.
- `src/replay.hpp/.cpp`: validate audit metadata, preserve order/timestamps, invoke shared book processing, finalize output and report decisions.
- `tests/replay_tests.cpp`: 115 targeted replay/reliability checks.
- `tests/replay_cli.cmake`: run the actual application twice, compare expected bytes and verify error exit codes/output preservation.
- `tests/fixtures/stage4e_market.csv`: 27 events, all three stream kinds, two epochs.
- `tests/fixtures/stage4e_expected_orderbook.csv`: 15 independently calculated book rows.
- `tests/fixtures/README.md`: explain each fixture event and expected decision.
- `.gitattributes`: preserve those fixtures' CRLF bytes.
- This validation record.

Modified:

- `src/config.hpp/.cpp`, `src/main.cpp`: `--replay CSV` selection, reject replay with duration, update help.
- `src/envelope.hpp/.cpp`: share inner-object validation between combined envelopes and already extracted replay payloads; each input JSON object is parsed once.
- `src/event_processing.hpp/.cpp`: retain the audit-first wrapper and expose the shared book-only path; invalidate on book-processing errors.
- `src/order_book.hpp/.cpp`: distinguish valid stored state from diff readiness for equal-ID short-snapshot checks.
- `src/session.cpp`: one explicit invalidation call in finalization; no TLS or endpoint changes.
- `CMakeLists.txt`: add application sources, one C++ test executable and one CLI integration test.
- `.gitignore`: allow the two deliberate fixture CSVs despite the existing `*.csv` rule.
- `.vscode/c_cpp_properties.json`: use the actual Stage 4E compilation database; no VS Code UI inspection is claimed.
- `README.md`, `docs/design.md`: document implemented replay and the actual policy changes.

Stage 4D's other untracked files remain present; Git's complete working-tree list therefore includes more files than this Stage 4E-only list.

## Exact sequencing and recovery behavior

The live client stops on a connection failure with nonzero status; it does not automatically reconnect. Session finalization clears book readiness. No retry, TLS recovery, REST bootstrap or backoff subsystem was added.

Replay uses stored `conn_epoch`. An increase clears levels, readiness, coverage and exchange revision watermarks, even if the new epoch starts with a trade. Epochs may start above zero or skip generations; a decrease fails. `conn_seq` must be positive and strictly increasing within an epoch; gaps are allowed because nonmarket messages may have consumed numbers. It may restart on a new epoch. Book `seqNo` starts at 1 and increments only for emitted rows, continuing across epochs. Exchange `U/u/lastUpdateId` are separate revision identifiers.

The approved limited coverage rule is preserved: `u <= L` is covered/stale; `U <= L+1 <= u` permits contiguous or overlapping advancement; `U > L+1` clears the book and waits. The highest valid observed revision survives a gap/coverage wait, so a snapshot behind that watermark is stale. No missing updates are buffered or reordered. A sufficiently fresh snapshot restores readiness only with five known levels on each side. Losing a fifth level when no known sixth can replace it clears state and emits no guessed row. Crossed candidates similarly clear and wait; malformed or contradictory payloads fail explicitly.

This remains a limited top-five model, conditional on comparable snapshot/diff revisions. It is not full-depth synchronization. The previously recorded live suppression concern remains: an earlier depth5 may cover a following diff entirely. Replay preserves that decision; it does not manufacture `D` rows.

## Replay interface and input contract

```powershell
.\build-stage4e\binance_capture.exe --replay tests/fixtures/stage4e_market.csv --output-dir build-stage4e/my-replay
```

Output: `build-stage4e/my-replay/stage4e_market_orderbook.csv`. Choose a fresh directory or absent target. Existing files are refused, never appended/overwritten. No new audit file is written. Replay selects its branch before constructing Session, DNS or a TLS context. The replay test executable itself links only the JSON dependency and ordinary C++ components, with no networking component.

The reader accepts the exact nine-column market header, quoted fields/doubled quotes, embedded CR/LF inside quoted fields, LF or CRLF record endings, and a final record without a newline. It holds one record at a time, bounded to `2 * 1024 * 1024 + 4096` raw bytes. This allows quote expansion of a 1 MiB message plus metadata. More than nine fields, bare CR outside quotes, NUL, malformed quotes, invalid headers/types/ranges, unsupported venue/shard/symbol/stream, invalid/non-object/non-minified JSON and invalid sequence order fail with logical record context. Minification validation uses the same ordered JSON representation as the audit writer.

Stored timestamps are used without sorting or sleeping, including a backward wall-clock second. Both mandatory schemas are unchanged. All successful applied depth events emit rows, including repeated matching snapshots and empty diffs that advance a valid revision. Trades remain audit-only. Valid header-only input is a successful empty replay. Exit 0 means complete valid input and successful output finalization; exit 1 means replay/open/write/semantic failure; exit 2 means invalid CLI. `--duration` is incompatible with replay. A failed replay may leave a header-only or earlier valid book prefix; output is not transactional.

## Commands actually executed

Repository root was the working directory. Child processes were launched through Python `subprocess.run` argument arrays with a process-local PATH prefix:

```text
C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin;<inherited PATH>
```

No registry, global PATH, certificate bundle, endpoint or dependency installation was changed. Existing tools were GCC 16.2.0 x64, CMake 4.4.4, Ninja 1.13.2, Boost 1.92.0, OpenSSL 3.6.5 and JSON 3.12.0.

The executable/argument combinations below were run as single commands; wrapping is for readability:

```text
C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\ctest.exe
  --test-dir build-stage4d -V --output-on-failure

C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\cmake.exe
  -S . -B build-stage4e -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=ON
  -DCMAKE_CXX_COMPILER=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\g++.exe
  -DCMAKE_MAKE_PROGRAM=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\ninja.exe
  -DCMAKE_PREFIX_PATH=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64
C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\cmake.exe --build build-stage4e --parallel 2
C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\ctest.exe --test-dir build-stage4e -V --output-on-failure
```

`build-stage4e` did not exist before configuration; the initial build compiled 48 steps. After adding the CLI test registration, CMake was rerun with `-S . -B build-stage4e`, followed by build/CTest. A final explicit `<utility>` include caused two CSV-reader object rebuilds and relinking; the final 8-test run passed again. Logs are `build-stage4e/configure.log`, `build.log`, `configure-final.log`, `build-final.log`, `tests-final.log`.

The registered CLI integration test ran:

```text
<build-stage4e/binance_capture.exe> --replay <tests/fixtures/stage4e_market.csv> --output-dir <build-stage4e/replay-cli-4b75aee8d2e0/first>
<build-stage4e/binance_capture.exe> --replay <tests/fixtures/stage4e_market.csv> --output-dir <build-stage4e/replay-cli-4b75aee8d2e0/second>
```

It used CMake `-E compare_files` against the independent expected CSV after each run, then executed existing-output, missing-input, incompatible-duration and malformed-header cases. The paths above identify the final run; randomized directories only avoid output collisions and do not enter CSV bytes.

The saved Stage 4D tape was also replayed entirely offline:

```text
build-stage4e\binance_capture.exe
  --replay output\stage4d-20261021T032849Z\market_data_spot_BTCUSDT_2026-10-21.csv
  --output-dir build-stage4e\replayed-stage4d
```

## Results and determinism

| Check | Actual result |
| --- | --- |
| Baseline Stage 4D CTest | Exit 0; 6 passed, 0 failed. |
| Fresh Release configure/build | Exit 0; no compiler warnings. |
| Final Release CTest | Exit 0; 8 passed, 0 failed; reported time 0.82 seconds. |
| Existing envelope tests | 4 classifications and 11 malformed-envelope cases passed. |
| Existing CSV/decimal/book tests | Existing CSV checks, 25 decimal cases and 120 book/pipeline checks passed. |
| New C++ replay/reliability suite | 115 checks passed. |
| Actual CLI fixture replay, twice | Both exit 0; each read 27 market rows and emitted 15 book rows matching independent expected bytes. |
| CLI error cases | Existing output 1; missing input 1; incompatible options 2; malformed header 1 with record context. Existing output hash unchanged. |
| Saved Stage 4D replay | Exit 0; 1,324 input rows regenerated 580 book rows byte for byte. |
| Git whitespace check | Exit 0; existing LF/CRLF conversion notices are not compiler warnings. |

New checks cover CSV quoting and bounds, input read errors, malformed metadata/JSON/decimals, integer overflow, header/field counts, repeated output sequences, gap recovery, epoch reset, known versus unknown sixth levels through the fixture, retained-level bounds, short-snapshot contradictions, fail-closed decoding, correct error-prefix output, and CLI compatibility.

Fixture decision counts are independently specified: snapshots 9, diffs 6, stale snapshots 2, stale diffs 1, waiting 4, gap 1, coverage loss 1, crossed 1, plus two trades. The expected file contains 15 data rows, 3,656 bytes, SHA-256 `5d28fa987a3aaf015df1566a72826e0a12f80f026140fc748f86ea727d653a6c`. Both actual CLI runs and both unit-level replays match it exactly.

Historical saved-tape regeneration produced 580 `P` rows and zero `D` rows, matching its original decisions: 580 applied snapshots and 580 covered diffs, with no waits/gaps. The regenerated file has 141,235 bytes and SHA-256 `82a7ac44b980fc3b01c9cfb141487c70530341ea6955fb6d75a0f109f0e8d259`, equal to the original Stage 4D book. Evidence is `build-stage4e/historical-replay.log` and `historical-replay.json`. This is offline regression evidence, not a new live capture.

## Sanitizer availability and additional diagnostics

Actual probes:

```text
<portable g++.exe> -std=c++17 -fsanitize=address -g -Isrc tests/decimal_tests.cpp src/decimal.cpp -o build-stage4e-check/asan.exe
<portable g++.exe> -std=c++17 -fsanitize=undefined -g -Isrc tests/decimal_tests.cpp src/decimal.cpp -o build-stage4e-check/ubsan.exe
```

Both link commands exited 1: `cannot find -lasan` and `cannot find -lubsan`, respectively. No sanitizer executable ran. Logs: `build-stage4e-check/asan.log`, `ubsan.log`. No packages were downloaded to work around this environment limitation.

An additional checked-container configuration was built and tested:

```text
<portable cmake.exe> -S . -B build-stage4e-checked -G Ninja
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
  "-DCMAKE_CXX_FLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS"
  -DCMAKE_CXX_COMPILER=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\g++.exe
  -DCMAKE_MAKE_PROGRAM=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin\ninja.exe
  -DCMAKE_PREFIX_PATH=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64
<portable cmake.exe> --build build-stage4e-checked --parallel 2 --target
  binance_envelope_tests binance_market_data_tests binance_decimal_tests binance_order_book_tests binance_replay_tests
<portable ctest.exe> --test-dir build-stage4e-checked -V --output-on-failure -E cli
```

Configure/build/CTest all exited 0; no compiler warnings; all five offline suites passed, zero failed, reported time 2.40 seconds. The application and CLI tests were deliberately excluded from this diagnostic build. Checked iterators/container assertions provide additional diagnostics; they do not replace ASan/UBSan or establish freedom from leaks. Logs are under `build-stage4e-checked/`.

## Unverified behavior and Stage 4F

- Current TLS failure and live connectivity were neither reproduced nor fixed here. Endpoints and verification settings are unchanged.
- Duration expiry, SIGINT/SIGTERM/Windows SIGBREAK, stalled/partial network failures and callback drainage were source-reviewed. No new live or injected transport/signal execution was performed, so those paths have no new runtime evidence.
- No automatic reconnect is implemented. Epoch correctness is tested from recorded input and pure book state; actual reconnect orchestration remains absent.
- Full-depth correctness is not claimed. Snapshot/diff comparability, coverage-based dropping and covered-diff acceptance remain explicit limitations from Stage 4D.
- Sanitizer/leak checks remain outstanding on a supported environment. Windows results do not establish Linux/GCC 12 behavior; Linux validation was outside this stage.
- Output is buffered and nontransactional. Read/write failures are checked, but a crash or partial filesystem write can leave incomplete artifacts. Blocking disk I/O can delay the one-thread live event loop.
- Replay handles the application's current Spot BTCUSDT/shard-0 schema and fixed policy; it does not synthesize control notices absent from the audit, reproduce transport-only counters, or support USD-M/multiple symbols.

After separate approval, Stage 4F should address the reported certificate-verification failure while retaining TLS verification, then obtain the needed runtime/network evidence and resolve final acceptance gaps. No Stage 4F work has begun. No commit, push, tag or submission was performed.
