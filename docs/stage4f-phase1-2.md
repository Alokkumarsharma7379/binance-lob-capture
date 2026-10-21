# Stage 4F — Phases 1 and 2

Results recorded October 21, 2026. Scope: preserve and review the Stage 4E workspace, run the current Release build/tests, diagnose TLS trust, and attempt a real capture. README finalization, Linux validation and GitHub submission were not started. No commit, push or tag was created.

## Phase 1: checkpoint and current implementation

- Git HEAD remains `eca42cbcdf2a4c383e615f819f75962263d8d3f9`.
- A 49-file checkpoint includes tracked and untracked project files, including Stage 4D/4E source, tests and fixtures. It excludes ignored build/output/tool artifacts. ZIP integrity checked successfully; its manifest records each file's SHA-256.
- Checkpoint: `build-stage4f-20261021T093124Z/workspace-checkpoint.zip`; manifest: `checkpoint-manifest.json` in that directory. Git status before this pass is saved as `git-status-before.txt`.
- `cmake --build build-stage4e --parallel 2` exited 0: `ninja: no work to do`. This verified the current Release build; it was not a new clean compilation. Stage 4E's clean build remains separately recorded.
- `ctest --test-dir build-stage4e -V --output-on-failure` exited 0: **8 passed, 0 failed**, reported time 0.63 seconds. This includes the independent replay fixture and actual application CLI tests.
- Replay is implemented in C++17, selects its branch before networking, and retains the existing book/decoder/formatter. The 27-event fixture and repeated CLI runs matched the 15 independently expected rows.
- The exact nine-column market header and 26-column book header remain unchanged. Existing tests and the new capture validators checked those contracts.
- Source review/search found no merge markers, TODO/FIXME markers or unfinished-operation placeholders in source/tests/CMake. No incomplete source change requiring repair was identified.
- No existing workspace file was changed in these phases. The application, order-book algorithm, CLI, endpoint, TLS code, README, requirements and design are unchanged. This report is the only newly added project document.

The original assignment's six-page extracted text, requirements/design documents, current networking initialization, schemas and CMake were reviewed. The PDF and requirements remain preserved in the checkpoint. Current pending Git changes still include all uncommitted Stage 4D/4E work; they have not been published.

## Phase 2: cause of certificate verification failure

The process initially had no `SSL_CERT_FILE`, `SSL_CERT_DIR` or `OPENSSL_CONF` override. Inspection found:

| File | Bytes | PEM certificates | Result |
| --- | ---: | ---: | --- |
| `<portable-ucrt64>/etc/ssl/cert.pem` | 0 | 0 | Unusable; loading reports no certificate or CRL. |
| `<portable-ucrt64>/etc/ssl/certs/ca-bundle.crt` | 0 | 0 | Unusable; same loading error. |
| `C:/Users/hp/.codex/tools/binance-stage4b/ca-bundle.crt` | 229,042 | 150 | Loads 150 CA certificates; portable OpenSSL successfully verifies Binance with it. |
| Existing Git for Windows `mingw64/etc/ssl/certs/ca-bundle.crt` | 229,042 | 150 | Byte-identical to the separate bundle above. |

The existing `ca-source.json` identifies the Git for Windows bundle as the source. Both nonempty files have SHA-256 `3e68a959493625de324485c7e0314e68688af96b6efdfd80fb7f99e804493459`. Python's SSL loader independently counted the certificates; the actual portable OpenSSL 3.6.5 executable performed the handshake comparisons below. No certificate or dependency was downloaded, replaced or installed during this pass.

The application already calls `set_default_verify_paths`, enables peer-chain verification, uses hostname verification for `stream.binance.com`, configures SNI for the same hostname, and requires TLS 1.2 or newer. No insecure bypass was present or added.

Both TCP ports were reachable during this investigation. Four portable OpenSSL probes used SNI, `-verify_hostname stream.binance.com`, `-verify_return_error` and `-brief`:

| Port | Default trust | Explicit existing bundle |
| --- | --- | --- |
| 443 | Exit 1, certificate verification failure | Exit 0, TLS 1.3, `Verification: OK` |
| 9443 | Exit 1, certificate verification failure | Exit 0, TLS 1.3, `Verification: OK` |

Default probes reported error 19 at DigiCert Global Root G2: self-signed certificate in certificate chain, indicating an untrusted chain root in that configuration. The verified probes reported the Binance Holdings Limited peer certificate and a matching `*.binance.com` identity. The previous curl timeout was not reproduced as a current TCP-connectivity blocker. These probes verify TLS on both ports, not application WebSocket capture on port 443.

The application itself was then run without the override. It reproduced `TLS handshake failed: certificate verify failed (SSL routines)`, exited 1 after 857 ms, and produced only two header-only files. These files are explicitly not a successful capture.

### Smallest verified correction

Set `SSL_CERT_FILE` to the existing usable trusted bundle for the application process. [OpenSSL documents this override](https://docs.openssl.org/3.6/man3/SSL_CTX_load_verify_locations/) for default certificate-file discovery. The same executable then completed chain/hostname verification, WebSocket setup and real market-data receipt. No application rewrite or port change was needed.

Actual child environment used:

```text
PATH=C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin;<inherited PATH>
SSL_CERT_FILE=C:\Users\hp\.codex\tools\binance-stage4b\ca-bundle.crt
```

Equivalent PowerShell launch, using a fresh output directory:

```powershell
$env:PATH = "C:\Users\hp\.codex\tools\binance-stage4b\ucrt64\bin;$env:PATH"
$env:SSL_CERT_FILE = 'C:\Users\hp\.codex\tools\binance-stage4b\ca-bundle.crt'
.\build-stage4e\binance_capture.exe --venue spot --symbols BTCUSDT --output-dir ./output/a-new-run --duration 60
```

The actual runs were launched through Python subprocess argument arrays with a child-only environment. No global PATH, registry or environment settings changed. The empty portable default files remain unchanged: a future process launched without the override will still have the original trust problem. This is an explicit launch-configuration correction, not an automatic default-store repair. Linux trust discovery was not tested in this phase.

Binance documents ports 443 and 9443 as supported Spot endpoints in its [official stream documentation](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md). Application capture retained the existing `stream.binance.com:9443` combined-stream endpoint and all three subscriptions.

## Real capture outcomes

### Requested 120-second attempt: failed transport run

Command:

```text
build-stage4e/binance_capture.exe --venue spot --symbols BTCUSDT
  --output-dir output/stage4f-120-20261021T093234Z --duration 120
```

With the explicit CA bundle, DNS, TCP, verified TLS and WebSocket setup succeeded. The program received real events, then counters stopped advancing. The WebSocket idle timeout ended the run after **110,093 ms**, exit **1**, with `The socket was closed due to a timeout`. This is not a completed 120-second sample. The specific reason for the transport silence remains undiagnosed.

The retained prefix has 2,424 market rows: 490 diffs, 491 depth5 events and 1,443 trades. It has 491 `P` book rows and no `D` rows. There were zero parse, I/O and book errors, one connection error, and no gap/coverage/crossing recovery. An independent CSV-aware count confirmed nine fields on every market row and 26 on every book row. These are valid recorded partial artifacts from a failed run, not proof of sustained connectivity.

### Fresh 60-second attempt: completed successfully

Command:

```text
build-stage4e/binance_capture.exe --venue spot --symbols BTCUSDT
  --output-dir output/stage4f-60-20261021T093503Z --duration 60
```

Run start: 09:35:03 UTC / 15:05:03 IST on October 21, 2026. Requested duration includes connection setup. It stopped for duration expiration, reported **60,403 ms** including shutdown and exited **0**.

| Result | Count |
| --- | ---: |
| Market rows | 2,431 |
| Differential events | 590 |
| Depth5 events | 590 |
| Trades | 1,251 |
| Book rows / applied snapshots | 590 |
| Applied diffs | 0 |
| Covered/stale diffs | 590 |
| Stale snapshots / waiting / gaps / coverage losses / crossed candidates | 0 / 0 / 0 / 0 / 0 |
| Nonempty same-revision checks | 443 |
| Parse / connection / I/O / book errors | 0 / 0 / 0 / 0 |
| Ping frames | 3 |
| TLS shutdown warnings | 1 |

The warning is the existing case of a normal WebSocket close code 1000 without TLS `close_notify` during intentional shutdown. It is distinct from the certificate-verification failure. No TLS setting was weakened to accept the capture.

Both existing CSV validators were run to verify the completed capture, and both exited 0. They checked every logical row, exact headers, ordering/metadata, integer bounds and scaling, book sorting, timestamps, event types, actual printed source samples and agreement with final counters. The original nine-column payloads contain actual Binance inner objects; no synthetic fixture was used for live output.

All 590 diffs were covered by earlier snapshots, so the book file again contains only `P` rows. This does not add live `D`-application evidence. The deterministic fixture still covers `D` behavior. Cross-stream comparability and acceptance of covered-diff suppression remain explicit limited-model concerns, and order-book logic was not changed during TLS diagnosis.

Files in `output/stage4f-60-20261021T093503Z/`:

| File | Data rows | Bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `market_data_spot_BTCUSDT_2026-10-21.csv` | 2,431 | 1,137,031 | `f0f1a2084cad1ec3d567656729f68ca7bdf2a04ab908162a2bc1ed5b2678f4cc` |
| `market_data_spot_BTCUSDT_2026-10-21_orderbook.csv` | 590 | 143,933 | `fd1723ffbde87d1b032b581aa9465f8660766bb9a2e2ea243fafd64ac921d068` |

The new audit was also replayed without network calls into `build-stage4f-20261021T093124Z/live-60-replay/`. Replay exited 0 and regenerated all 590 book rows **byte for byte**, with the same book SHA-256 and decision counts.

## Evidence and remaining work

Evidence directory: `build-stage4f-20261021T093124Z/`.

- `baseline-build.log`, `baseline-tests.log`: current Release/CTest output.
- `ca-inspection.json`, `tcp-probes.json`, `tls-probes.json` and individual `tls-*.log`: actual CA and connectivity comparisons.
- `default-app.*`, `live-120.*`, `live-60.*`: commands, process exit codes, stdout samples and stderr summaries.
- `initial-output-integrity.json`: distinction between header-only failure and recorded failed-run prefix.
- `market-validation.log`, `pair-validation.log`, `live-replay.log`, `validation-results.json`: completed-capture checks and replay byte comparison.
- `preservation-audit.json`: checkpoint integrity, unchanged existing project files and executable hash.

The application SHA-256 is `760f32c1b5da15ad81e94dad70f184bfcc518102282d4efffda465c1bea17e70`. No source or test code was edited in these phases. Existing Stage 4D/4E changes remain pending in Git; generated evidence and captures are ignored as before.

Remaining issues before a submission-ready claim: unexplained timeout in the longer run; Linux/GCC 12 validation not yet performed; sanitizer runtime unavailable in the existing Windows setup as recorded in Stage 4E; limited-book synchronization/covered-diff acceptance concerns; final README/sample accessibility and GitHub audit still pending. No automatic reconnect is implemented. No claim of unconditional correctness or stable 120-/300-second capture is made.

Phases 1 and 2 stop here for user review. README finalization, publication, tagging and GitHub submission require the next explicit instruction.
