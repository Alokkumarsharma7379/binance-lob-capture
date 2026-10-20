# Stage 4C independent workspace verification

Verified on 2026-10-21 (local time), using actual workspace files and terminal processes. This audit did not implement Stage 4D or change production code.

## Results

| Check | Result | Evidence / limitation |
| --- | --- | --- |
| Source implementation exists | PASS | Read `src/envelope.cpp`, `src/market_data.hpp/.cpp`, `src/session.cpp`, CMake and tests. Inner JSON extraction, timestamp/sequence assignment, escaped nine-field rows, exclusive file creation, periodic flush and checked finalization are implemented and connected. |
| Clean Windows Release build | PASS | Fresh `build-stage4c-verify`, GNU 16.2.0, CMake 4.4.4, Ninja. Configure exit 0, build exit 0; all 13 build steps completed. No compiler warnings or errors appeared. |
| CTest | PASS | All four tests passed; CTest exit 0. Full actual output: `build-stage4c-verify/ctest.log`. |
| Direct unit-test executables | PASS | Both exited 0; outputs saved in `envelope.log` and `market.log` in the new build directory. |
| CLI regression | PASS | 19/19 expected exit codes. Help exits 0; invalid arguments exit 2. Exact arguments, stdout, stderr and exit codes: `build-stage4c-verify/cli-regression.json`. |
| Saved live CSV | PASS | All 4360 data rows have nine logical fields, checked with Python's CSV reader. Exact header, all three streams, JSON, metadata and timestamps pass the existing validator, exit 0. |
| CSV counts against capture log | PASS | 590 depth diffs + 590 depth5 snapshots + 3180 trades = 4360. These equal the final session counters. |
| VS Code Problems | PARTIALLY VERIFIED | No actual compiler errors in a clean build. No workspace IntelliSense configuration existed; the compiler on default PATH is MinGW GCC 6.3.0. Added explicit portable GCC/C++17/compilation-database configuration. The session has no Problems-panel API; exact diagnostic text was requested but not received. Cannot conclusively identify or claim removal of the two reported Problems. |
| Git diff/history | UNAVAILABLE | `git status --short` exits 128: `fatal: not a git repository (or any of the parent directories): .git`. No repository was initialized. Historical Git changes/deletions cannot be reconstructed. |
| Preservation during this audit | PASS | SHA-256 comparison of 21 pre-existing non-build project/data files: zero modifications and zero deletions. The PDF, requirements, design, existing source/tests and saved capture are preserved. |

The initial sandboxed configure stalled in the compiler ABI probe and was stopped (exit 4294967295). The same toolchain configured and built successfully outside that sandbox in a separate fresh directory. The stalled `build-stage4c-audit` folder is not the successful build. No system packages, global PATH or registry settings were changed.

## Commands and actual test output

Clean build commands executed, with environment changes confined to the process:

```powershell
$stagePrefix = 'C:\Users\hp\.codex\tools\binance-stage4b\ucrt64'
$env:PATH = "$stagePrefix\bin;$env:PATH"
& "$stagePrefix\bin\cmake.exe" -S . -B build-stage4c-verify -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=ON "-DCMAKE_CXX_COMPILER=$stagePrefix/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$stagePrefix/bin/ninja.exe" "-DCMAKE_PREFIX_PATH=$stagePrefix"
& "$stagePrefix\bin\cmake.exe" --build build-stage4c-verify --parallel 2
```

CTest command: `<portable-prefix>\bin\ctest.exe --test-dir <workspace>\build-stage4c-verify -V --output-on-failure`. Tests were launched using a Python subprocess with an explicit child `PATH` beginning with the portable `bin` directory, because the sandbox launcher can drop shell-local environment changes.

Actual output excerpts:

```text
1/4 Test #1: envelope_validation .............. Passed
2/4 Test #2: market_data_csv .................. Passed
3/4 Test #3: cli_help ......................... Passed
4/4 Test #4: cli_rejects_usdm .................. Passed
100% tests passed out of 4
Total Test time (real) = 0.36 sec
EXIT ctest 0

Passed 4 stream classifications and 11 invalid-envelope cases.
EXIT envelope 0

Passed CSV round trips, inner JSON preservation, metadata, timestamps, sequences, exclusive file creation and file-error checks.
EXIT market 0

Usage: binance_capture [options]
EXIT help 0

Argument error: --venue supports only spot in the initial scope
Use --help for usage.
EXIT invalid-venue 2
```

The intentionally invalid CLI exit 2 is an expected pass, not a failed test. Full unabridged outputs and machine-readable exit codes are in the new build directory, including `test-exits.json`.

## CSV verification

Checked the existing 60-second capture; no new live run was performed in this audit:

`output/market_data_spot_BTCUSDT_2026-10-20.csv`

```text
Bytes: 1900809
Data rows: 4360
Logical column counts: {9: 4360}
depth_diff: 590
depth5: 590
trade: 3180
Final conn_seq: 4360
Validator exit: 0
SHA256: 9e49e6ab15a3f816ef94dcf72e7c5f792b24fbc0221658bdcfd55e7792434983
```

```text
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
```

Executed:

```powershell
python tests/validate_market_csv.py output/market_data_spot_BTCUSDT_2026-10-20.csv --session-log build/live-60-stage4c.stderr.log --samples-log build/live-60-stage4c.stdout.log
```

Also counted independently using `csv.reader(..., strict=True)` and compared the results with the final log tuple `(Messages, depth_diff, depth5, trade, market_rows) = (4360, 590, 590, 3180, 4360)`. The validator checked all rows and matched the three printed original source samples to the saved inner payloads. Output is preserved in `build-stage4c-audit/csv-validation.log`; independent counts are in `csv-counts.json` alongside it.

The original capture log records exit 0 with one TLS-close warning, as explained in `stage4c-validation.md`. This CSV audit does not validate order-book reconstruction or exchange sequence continuity.

## Editor configuration and file preservation

Added only these non-build files during this audit:

- `.vscode/c_cpp_properties.json`: workspace-only IntelliSense setup, portable GCC 16.2.0, C++17, Windows x64 GCC mode and `build-stage4c-verify/compile_commands.json`. Verified JSON syntax, referenced compiler/database existence, and C++17 commands for `market_data.cpp`. The installed C/C++ extension schema supports these settings. If this build folder is later removed or renamed, update the compilation-database path.
- `docs/stage4c-audit.md`: this record.

No existing source, test, CMake, requirement, design or capture file was edited. `build-stage4c-audit/before-sha256.json` records the baseline for this audit. The requirements SHA-256 still matches the earlier recorded value: `3428ef37f2a671831a5f95661038e04a81733368a7c1502f5d64dfe1d1ddcb83`.

The successful build rules out compiler errors under the configured toolchain. It does not reveal the exact diagnostics currently shown in VS Code or prove that the editor has refreshed them. If the two Problems remain, their text/source is needed to finish that specific check. Linux/GCC 12 verification remains outstanding.
