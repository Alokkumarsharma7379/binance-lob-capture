# iRage assignment requirements

Source: [Software Developer - Assignment (2).pdf](../Software%20Developer%20-%20Assignment%20%282%29.pdf), all six pages, including tables, examples, submission instructions, and reviewer checklist. Both extracted text and rendered pages were reviewed.

This is a requirements analysis, not an architecture or implementation. No implementation choices below are approved merely by appearing in this document. Instructions in the PDF describe the eventual assignment submission; they do not authorize building, running a capture, publishing a repository, or tagging a release during this stage.

**Reading guide:** “Required” identifies an explicit assignment contract. “Review expectation” identifies an item reviewers are told to assess, without turning every suggested technique into a mandatory feature. “Optional” preserves explicit options and conditional requirements. “Unresolved” identifies a choice or inconsistency the PDF does not settle. Small examples explain behavior; they are not additional acceptance criteria.

Page references:

| PDF page | Material |
| --- | --- |
| 1 | Task, language, official references, endpoints, three subscriptions, market-file naming and CSV rules |
| 2 | Market columns, time and ordering policies, book-row emission, book header and column meanings |
| 3 | Book timestamps, scaling, reconnects/gaps, submission deliverables, six functional acceptance criteria, correctness review |
| 4 | Remaining review criteria, optional stretch features, beginning of GitHub submission instructions |
| 5 | GitHub submission, ignore rules, configuration guidance, prerequisites and build example |
| 6 | Run examples, sample output names, schema/test commands, final reviewer checklist |

## 1. Exact problem in simple language

Build one coherent **C++** program that listens to Binance public market data for configured instruments, such as `BTCUSDT`, and produces two related records:

1. A **market-data CSV**: an audit trail of incoming logical Binance events, their timestamps, connection identity, and payloads.
2. An **order-book CSV**: the top five buy prices and top five sell prices, with their quantities, after events are applied to the local book.

The events must be processed in an order reflected by the audit file. The book output must follow the exact column contract and use deterministic scaled integers for prices and quantities.

The opening paragraph says to emit a book snapshot after each processed event. Deliverable B provides the more specific rule: at least every **applied `depthUpdate` and `depth5`**, while book rows for **trades are optional**. Capturing trades in the market-data CSV remains required. This distinction must be preserved.

C++17 or newer is **recommended**; C++ itself is required. The README must name the actual compiler and standard. Original code or extensions to a provided codebase are allowed. The current workspace supplied for this stage contains the assignment PDF, not a provided implementation.

This is a market-data capture and book-maintenance assignment. The PDF does not request order placement or a trading strategy.

## 2. Inputs, including all three streams

### Runtime configuration

The PDF requires configured symbols and documentation of the symbol-list format and exact CLI. Its examples use:

| Example argument | Meaning and qualification |
| --- | --- |
| `--venue spot` or `--venue usdm` | Binance product/venue. Both are shown; whether both must be implemented is not explicitly settled. |
| `--symbols BTCUSDT` | One instrument. |
| `--symbols BTCUSDT,ETHUSDT` | Example comma-separated multi-symbol input; multi-symbol support is explicitly optional stretch. |
| `--output-dir ./output` | Output location in the live-run examples. |
| `--duration 300` | Used in the final reviewer checklist for a 300-second live run. It is a concrete reviewer expectation despite not appearing in the earlier CLI examples. |

The PDF does not specify argument defaults, complete validation rules, or a configuration-file schema.

### Required subscriptions per supported symbol

All three streams must be captured through Binance **combined streams**. Symbols in stream URLs are lowercase; the CSV `symbol` value is uppercase.

| Exact subscription suffix | Market CSV `stream_kind` | Role in the assignment |
| --- | --- | --- |
| `<symbol>@depth@100ms` | `depth_diff` | Differential depth, described as `depthUpdate`; changes price levels in the book. |
| `<symbol>@depth5@100ms` | `depth5` | Partial top-of-book snapshot, described as a sanity check / refresh; replacement semantics matter. |
| `<symbol>@trade` | `trade` | Individual trades; must be captured, but do not automatically modify a diff-based book. |

The `@100ms` suffixes are part of the specified depth subscriptions. They are not a requirement to write CSV on a separate 100 ms timer. Output is event-driven.

PDF example for one Spot symbol:

```text
btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade
```

The PDF gives these example combined-stream base URLs:

```text
Spot:  wss://stream.binance.com:9443/stream?streams=
USD-M: wss://fstream.binance.com/public/stream?streams=
```

It specifically says raw USD-M `@trade` is on the `/public` path. These are transcribed assignment references, not endpoints validated by a live capture during this stage.

The PDF names the following as authoritative for actual stream semantics and URL patterns:

- [Spot WebSocket market streams](https://developers.binance.com/docs/binance-spot-api-docs/web-socket-streams)
- [Spot combined streams](https://developers.binance.com/docs/binance-spot-api-docs/web-socket-streams#combined-streams)
- [USD-M Futures WebSocket market streams](https://developers.binance.com/docs/derivatives/usds-margined-futures/websocket-market-streams)

The PDF does **not** reproduce full Binance JSON schemas. It explicitly references an outer envelope, an inner `data` object, decimal strings, possible time fields `E` and `T`, and diff sequencing fields `U`, `u`, and `pu`. Product-specific field presence and exact synchronization rules must be checked against the official references before implementation. Attempts to retrieve those pages in this stage did not yield usable documentation; this document does not claim independent verification of current Binance behavior.

Conceptual envelope example, supplied here for explanation rather than copied from the PDF:

```json
{"stream":"btcusdt@depth@100ms","data":{"...":"logical event payload"}}
```

The audit column `payload_json` stores the inner object, not this entire wrapper. `stream_kind` identifies the subscription category; it should not be confused with the one-letter book CSV `type`.

Optional replay input is the program's own saved `market_data_*.csv`. If replay is supplied for grading, it must reproduce the book without network calls.

## 3. Output files and exact schemas

### Rules shared by both CSV outputs

Required: UTF-8, comma-separated, RFC 4180-style escaping, and the header as the first row. A field containing a comma, quote, or newline must be double-quoted; a quote inside it becomes `""`. Do not silently truncate data.

Example: the logical JSON value `{"p":"100.00","q":"2"}` becomes this single CSV field:

```csv
"{""p"":""100.00"",""q"":""2""}"
```

Splitting a market CSV line on every comma is not a valid schema check because quoted JSON contains commas. Use a CSV-aware reader for that file.

### Deliverable A: market-data audit CSV

PDF filename pattern:

```text
market_data_<venue>_<SYMBOL>_<UTC-date>.csv
```

An alternative is one file per run with a `symbol` column for multi-symbol capture; document the choice. The `symbol` column is part of the required schema even for a per-symbol file.

The following exact header is assembled in the fixed order of the PDF's required-column table:

```csv
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
```

| Column | PDF type / allowed values | Meaning |
| --- | --- | --- |
| `recv_tsec` | `int64` | Seconds component of the selected receive/event timestamp policy. |
| `recv_tnsec` | `int32`, `0` through `999999999` | Nanosecond remainder of that timestamp. |
| `venue` | `spot` or `usdm` | Venue for this event. |
| `stream_kind` | `depth_diff`, `depth5`, `trade`; alternatively documented `0`, `1`, `2` | Logical stream category. Document the mapping if numeric values are used. |
| `shard_id` | Non-negative integer | Connection shard index; `0` for a single connection. |
| `conn_epoch` | Non-negative integer | Connection generation; increment on **each WebSocket reconnect**. |
| `conn_seq` | Monotonic within `(shard_id, conn_epoch)` | Processing sequence associated with that connection generation. Width, starting value, and reset convention are not specified. |
| `symbol` | Uppercase string, for example `BTCUSDT` | Instrument for the event. |
| `payload_json` | Minified, one-line JSON object, CSV-escaped | Inner Binance `data` payload. Not the combined-stream envelope. |

There are **nine columns**.

Rows must reflect **processing order**, the same order the LOB consumer sees. The introduction says “one row per inbound event,” while Deliverable A says “one row per message your process applies from the socket.” The treatment of malformed, stale, or intentionally dropped messages is unresolved; do not silently interpret this as permission to discard arbitrary received market events.

Timestamp policy: choose and document one of these:

- **Recommended:** wall-clock time when the process finishes reading the message, split into seconds and nanoseconds.
- **Allowed alternative:** Binance event time from JSON; identify the field (`E`, `T`, etc.) and its units.

### Deliverable B: order-book snapshot CSV

Required filename suffix:

```text
*_orderbook.csv
```

The PDF's paired example filenames are:

```text
output/market_data_spot_BTCUSDT_2025-01-15.csv
output/market_data_spot_BTCUSDT_2025-01-15_orderbook.csv
```

The date is an example, not a required literal filename.

For each supported symbol, emit a row after each event that changes or refreshes the modeled state, **at minimum every applied `depthUpdate` and `depth5`**. Trade-triggered rows are optional, with documented effect or annotation. A message updating several levels results in a snapshot after the event, not a required row for every individual level.

**Source limitation:** the single-line header on PDF page 2 is visibly clipped at its right edge, ending around `ask_siz`. The complete header below is reconstructed from the explicit column-semantics table on that page and the repeated requirement for 26 columns on pages 3 and 6. The missing tail is disclosed rather than treated as visible verbatim text:

```csv
tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,ask0,ask1,ask2,ask3,ask4,ask_size0,ask_size1,ask_size2,ask_size3,ask_size4
```

| Columns, in exact order | PDF type | Meaning |
| --- | --- | --- |
| `tsec` | `int64` | Integer seconds for the row timestamp. |
| `tnsec` | `int32` | Nanosecond remainder in `[0, 999999999]`; `tsec * 1000000000 + tnsec` gives the timestamp in nanoseconds. |
| `seqNo` | `uint64` | Monotonic application sequence, incremented once per emitted row. |
| `id` | `int32` | Stable numeric instrument ID for the session; derivation must be documented. |
| `type` | `char` | One ASCII letter describing the last applied event class; mapping must be documented. The PDF does not prescribe the letters. |
| `side` | `char` | `B` = bid-side, `S` = ask-side, `N` = symmetric / not applicable. |
| `bid0`, `bid1`, `bid2`, `bid3`, `bid4` | Each `int64` | Five bid prices, best first, expressed as scaled integers. |
| `bid_size0`, `bid_size1`, `bid_size2`, `bid_size3`, `bid_size4` | Each `int64` | Quantities at the corresponding bid prices, using documented quantity scaling. |
| `ask0`, `ask1`, `ask2`, `ask3`, `ask4` | Each `int64` | Five ask prices, best first, expressed as scaled integers. |
| `ask_size0`, `ask_size1`, `ask_size2`, `ask_size3`, `ask_size4` | Each `int64` | Quantities at the corresponding ask prices, using documented quantity scaling. |

Count: six metadata fields + five bid prices + five bid quantities + five ask prices + five ask quantities = **26 columns**. Preserve capitalization, notably `seqNo`.

Best bid means highest buy price, so bids run from highest to lowest. Best ask means lowest sell price, so asks run from lowest to highest. Index `0` is the best level on each side.

Book timestamps should preferably use the same source as the market-data row for the triggering event; Binance time is also allowed if documented.

Fixed price and quantity scales and deterministic conversion are mandatory. The PDF gives `10^8` as an **example**, not a prescribed scale. Its table says sizes use “same scaling,” while the detailed scaling paragraph calls for fixed price and quantity scales; whether they must be equal is worth making explicit.

Explanatory example only: with a chosen price scale of `100`, `"101.25"` becomes `10125`; with a chosen quantity scale of `1000`, `"2.500"` becomes `2500`. These sample factors are not selected project policies. CSV integer columns must contain integers, with no binary floating-point artifacts.

## 4. What a local order book is

A local order book is the program's current view of outstanding buy and sell quantities at each price for an instrument. A price level represents quantity at a price, not necessarily one individual order.

Illustrative state:

| Side | Price | Quantity |
| --- | --- | --- |
| Bid | 100 | 2 |
| Bid | 99 | 5 |
| Ask | 101 | 3 |
| Ask | 102 | 4 |

The best bid is `100`; the best ask is `101`.

### Differential depth

A diff identifies levels to update. The quantity is the resulting quantity at that level, rather than an amount to add to the old quantity. A zero quantity removes a level; this deletion rule is explicit in the PDF's acceptance criteria.

For the example above:

1. Bid `(100, 7)` changes quantity at `100` from `2` to `7`, not `9`.
2. Bid `(99, 0)` removes bid price `99`.
3. Ask `(100.5, 1)` introduces a new best ask at `100.5`.
4. Other levels not mentioned by the diff remain unchanged.

After applying the event, emit the required book snapshot. Absolute-quantity behavior explains the “correct diff semantics” requirement; exact product-specific initialization and sequencing rules remain governed by the official Binance references.

### Partial `depth5` snapshot

This is a replacement/refresh of the supplied top-of-book view, not an incremental list of quantity changes. The PDF explicitly says reviewers check `depth5` replace semantics.

Example: the previous displayed bids are `[100, 99, 98, 97, 96]`, and a new `depth5` snapshot lists `[101, 100, 99, 98, 97]`. The refreshed five-level view is the new list. Merely appending `101` and retaining the old displayed five is incorrect.

The absence of `96` from the new top five does **not by itself establish that price 96 has zero quantity in the entire exchange book**; it may simply be outside the visible five. The PDF does not define how to reconcile a partial replacement with any deeper local state. That is an explicit design decision, not permission to claim `depth5` is a full-depth snapshot.

### Trades

A trade reports an execution. The assignment does not require automatically deducting its size from the diff-based book. The depth stream can already describe the resulting quantity changes, so independently deducting a trade without a defined integration rule can count an execution twice.

Example: local ask quantity is `3`; a trade reports quantity `1`; a subsequent diff sets that ask quantity to `2`. The diff already communicates the new size. Trade capture is mandatory; book mutation or annotation based on trades must be deliberately defined if included.

## 5. Mandatory, recommended, and optional scope

| Status | Feature or obligation | PDF location |
| --- | --- | --- |
| Required | C++ solution; state compiler and C++ standard | pp. 1, 3 |
| Recommended | C++17 or newer | p. 1 |
| Required | Live combined-stream capture of all three specified streams for supported symbols | pp. 1, 3 |
| Required | Market CSV schema, escaping, logical payloads, connection metadata, processing order | pp. 1–3 |
| Required | Local book with correct diffs, zero-size removal, `depth5` replacement/refresh, ordered top five | pp. 2–4 |
| Required | Exact 26-column book schema and rows for every applied diff and `depth5` | pp. 2–3 |
| Required | Deterministic scaling and documented timestamp, ID, type, sequence, reconnect/gap policies | pp. 2–3 |
| Required | CMake, or justification plus a reproducible build for a common Linux toolchain | p. 3 |
| Required | README and matching sample capture/book outputs | p. 3 |
| Review expectation | Performance, resource ownership, error handling, build warnings, observability, security and operational limits | pp. 3–4 |
| Optional | Book rows for trades; document mutation or annotation if supplied | p. 2 |
| Optional / encouraged by review | Replay from saved market CSV | pp. 3–4 |
| Required if replay supplied | No network calls during grading replay; state this in README | p. 3 |
| Explicit optional stretch | REST depth snapshot plus diff-buffer resynchronization | p. 4 |
| Explicit optional stretch | Multi-symbol capture with correct sharding | p. 4 |
| Explicit optional stretch | Throughput/latency performance notes | p. 4 |
| Optional techniques | Buffered writes, dedicated writer thread, pooling, ring buffers and similar allocation optimizations | p. 4 |
| Conditional | If multithreaded, clear synchronization, no data races; test commands are “if applicable” | pp. 4, 6 |
| Submission instructions | GitHub submission section in README, repository push, ignore rules, submission tag | pp. 4–5 |

Replay being optional does not remove the functional requirement that fixed input produce reproducible output. Likewise, optional REST resynchronization does not remove the requirement to state and consistently implement gap/reconnect behavior.

The PDF does not mandate a particular JSON library, network library, number of threads, internal container, throughput number, latency bound, or benchmark framework.

## 6. Edge cases and their required treatment

| Case | What the PDF requires or implies | What remains to decide |
| --- | --- | --- |
| Zero quantity in a diff | Remove the price level; do not retain it as a live zero-sized level. | No explicit rule for deletion of an already absent level; a harmless no-op is an example policy, not a PDF mandate. |
| Nonzero quantity at an existing price | Correct differential semantics replace its quantity. | Decimal precision/overflow rejection policy. |
| One message changes several prices/sides | Apply the event and report the resulting top five; preserve processing order. | Exact `side` rule for mixed/no-op events. `N` is defined as symmetric / N/A. |
| Applied refresh leaves visible prices unchanged | Minimum emission rule still says every applied diff and `depth5`; unchanged visible top five is not a general reason to suppress a required row. | Define what counts as “applied” for stale or ignored events. |
| `depth5` refresh | Replacement semantics for the modeled partial view; emit a row when applied. | Interaction with deeper state, diffs already applied, and snapshot/diff update IDs. |
| Fewer than five known levels / empty side | Output still has exactly 26 fields. | No PDF padding/sentinel policy; do not assume blank or zero without documenting a decision. |
| Best level removed | Recompute best-first ordering from valid known state. | If the next level was never observed, a top-five-only view cannot invent it; depth retention and refresh policy need a decision. |
| Duplicate, stale, out-of-order, or missing diff | Handle sequence information and document consistent behavior. | Venue-specific accept/drop/resync rules, audit inclusion, and row emission during recovery. |
| WebSocket reconnect | Increment `conn_epoch`; define effect on the local book and sequence state. | Initial epoch, `conn_seq` restart value, waiting/clearing/resynchronizing policy. |
| Parse failures / partial reads | Reviewers expect errors logged or counted and no silent undefined behavior. | Exact failure recovery and treatment in the audit log. The required payload schema cannot hold arbitrary non-JSON bytes as a valid inner object. |
| Timestamp absent or unsuitable | Pick and document a timestamp source and units. | A fallback for payloads without the chosen time field, and whether per-stream sources are permitted/desired. |
| Same timestamp / backward wall-clock movement | Audit and book processing order must remain consistent. | Do not assume timestamp values alone uniquely order events; define tie/order policy without inventing a PDF-required sorting algorithm. |
| Decimal too precise / integer overflow | Deterministic scaling; no silent truncation or floating-point artifacts. | Accepted precision, rejection/rounding policy, and range checks. |
| Shutdown, slow disk, socket pressure | Review expectations include safe teardown on `SIGINT`/`SIGTERM`, I/O/back-pressure consideration, and limits. | Flush, drain, retry, and limit policies. |

Three distinct sequences must not be confused:

- `conn_seq`: our audit sequence within one `(shard_id, conn_epoch)`.
- `seqNo`: our book-output sequence, incremented once per emitted row.
- Binance `U`, `u`, `pu`: exchange diff sequencing fields referenced by the assignment. Conventionally these describe first/final update IDs and a previous final update ID where available; they are not our CSV counters. Confirm the fields and exact rules for the selected product.

Conceptual gap example: if an event links to an exchange update that the program has not processed, the local state may be missing changes. The assignment requires a documented reset, resync, or drop-until-snapshot policy. This example does not prescribe a universal `U/u/pu` formula for both venues.

Reconnect example: audit keys `(0, 0, 40)` and then `(0, 1, 0)` could be valid under a documented zero-based restart policy. Only the epoch increment and within-epoch monotonicity are prescribed here; those initial/reset numbers are illustrative.

Timestamp example: `1700000000123` milliseconds converts to `tsec = 1700000000`, `tnsec = 123000000`. A nanosecond field does not mean the source clock actually has nanosecond precision.

## 7. Ambiguities and explicit decisions needed later

These are unresolved requirements questions, not architecture decisions made in this document.

| Issue | Source tension or omission | Decision to document or clarify |
| --- | --- | --- |
| Venue coverage | Both Spot and USD-M references, values, and run examples appear; no explicit “must implement both” statement. | Which venue(s) constitute the submission's supported scope. Do not silently promise or exclude USD-M. |
| Exact book header | Printed single-line header is clipped, but the column table and 26-column checks are complete. | Use the disclosed reconstruction in §3; obtain confirmation if literal source text is demanded. |
| Book row frequency | Opening summary says every processed event; detailed contract makes trades optional. | Apply the detailed minimum and state trade-row policy. |
| Audit coverage | “Inbound event” versus “message your process applies.” | Whether valid stale/rejected diffs remain in the audit; how malformed data, transport messages, and drop decisions are recorded. |
| Chronological versus processing order | Opening uses “chronological”; audit section explicitly requires processing order. | Define arrival/processing order, ties, and any cross-stream/shard ordering; do not silently sort by exchange timestamp. |
| Initial book correctness | Book must reflect correct diffs; full REST snapshot plus diff buffer is optional. | Initial readiness, depth coverage, what is known before first refresh, and what correctness can be claimed without a full snapshot. |
| Partial refresh and diffs | `depth5` is called sanity check / refresh, while review specifies replacement semantics. | Define exactly what is replaced and how valid deeper state and newer diffs are reconciled. |
| Recovery rules | Reset, resync, or drop until snapshot are listed without choosing one. | Per-venue handling of reconnect, gaps, duplicates, stale events, and resumption. |
| Book timestamp | Same source as audit is recommended; Binance time also allowed. | Source, units, absent-field fallback, ties and clock changes. |
| Scaling | Fixed price and quantity scales required; table uses “same scaling.” | Exact factors, equality or independence, per-symbol variation if any, excess precision, and overflow. |
| Missing levels | Five slots per side, no missing-value convention. | Padding/sentinel and startup/recovery behavior while preserving types and 26 columns. |
| Instrument IDs | Stable session ID, derivation unspecified. | Mapping, collision avoidance, and recovery of the mapping for reproducible replay. |
| Event type / side | One-letter `type` mapping unspecified; `B/S/N` meanings provided. | Type letters, mixed-side events, refreshes, no-ops and optional trades. |
| Sequence scopes | `conn_seq` scope specified, starts unspecified; `seqNo` scope across multiple instruments/files not explicit. | Initial values, reset rules, global versus per-symbol book sequence, and overflow behavior. |
| File lifecycle | Date-based pattern or per-run alternative; no rotation/append policy. | UTC date basis, midnight rollover, repeated runs, header duplication prevention, and book-file grouping. |
| Capture duration | Page 6 comment says “2-minute capture,” but that command has no duration flag; reviewer uses `--duration 300`. | Support/document the reviewer invocation and define default stop behavior. Do not infer a two-minute default. |
| CLI and configuration | CLI examples coexist with “All config should use environment variables or a .env.example template.” | Explain configuration sources and precedence without silently removing the example CLI. |
| Samples and ignore rules | Sample CSVs required; suggested `.gitignore` excludes `*.csv`. | Supply README excerpts or an attachment as expressly allowed, or document a deliberate sample-file exception. |
| Example toolchain | General requirement allows compiler/standard choice and justified non-CMake build; template names GCC 12/C++17 and says tested with Clang 15. | State and test actual tools. Do not copy “also tested” as a claim without evidence. |
| Complexity target | Review asks for top-five updates in `O(updates)` and explanation of full-map trade-offs. | Explain achieved complexity honestly; the PDF does not select a container or waive correctness to meet a slogan. |

There are no specified numeric performance thresholds, retry intervals, memory limits, maximum symbol counts, rounding mode, or full list of accepted errors. Such values must be explicit project decisions if introduced later.

## 8. What reviewers check and what must be submitted

### Functional acceptance in the PDF

The six criteria on page 3 are:

1. Live Binance combined capture with `depth@100ms`, `depth5@100ms`, and `trade`.
2. Market CSV column contract and row-per-event ordering.
3. Exact book header and **26 data columns per row**.
4. Correct top-five diff semantics, including removal when quantity is zero.
5. Documented integer scaling and timestamps; reproducible output from fixed input.
6. Stated reconnect, sequence, and epoch behavior.

### Code and performance review

| Area | What reviewers are told to look for |
| --- | --- |
| Market-data correctness | Diff versus `depth5` replacement; no unexplained trade mutation; `U/u/pu` handling; consistent reconnect epochs; correct best-first ordering. |
| Determinism and CSV | Fixed integer scaling; no `double` in the conversion hot path unless justified and lossless; exact headers; RFC 4180 payload escaping; no silent truncation. The earlier schema requirement still demands integer CSV columns. |
| Ownership and lifetime | RAII: resources are owned and cleaned up predictably; no dangling references in asynchronous callbacks; socket/file teardown on `SIGINT`/`SIGTERM`; leak-free, sanitizer/Valgrind-friendly behavior. |
| Errors | Reconnect paths, partial reads, and parse errors logged or counted, rather than silent undefined behavior. |
| Modern C++ | Appropriate `constexpr`, `enum class`, scoped types and `const` correctness; avoid unnecessary macros. |
| Build hygiene | `-Wall -Wextra` or stricter; documented Release build; no unexplained warnings. |
| JSON work | Efficient parser or documented fallback, no unnecessary repeated parsing, safe buffer reuse. A specific SIMD parser is not mandatory. |
| Allocations | Minimize per-message heap allocation and string churn. Pooling, `reserve`, ring buffers, or small-buffer-friendly patterns are suggestions, not mandatory structures. |
| Book work | Top-five updates should be `O(updates)` per message; explain trade-offs against a full-map book. |
| I/O | Consider file/socket back-pressure, meaning input can arrive faster than downstream work completes. Document the chosen approach and why; buffering or a writer thread is optional. |
| Threads | If used, clear boundaries and synchronization, no races, and avoidance of highly contended per-tick locks when claiming high throughput. |
| Observability | Counters for processed messages, parse errors, reconnects and written rows help reviewers verify robustness. |
| Replay | Offline regeneration of book CSV helps review and regression; it is encouraged, not unconditional in the submission requirements. |
| Security/operations | No repository secrets; do not disable TLS verification without strong justification; reasonable URL/shard limits to avoid runaway memory. |

### Deliverables and README content

Submit C++ source, CMake or a justified reproducible common-Linux build, and a README containing:

- Compiler, C++ standard, dependencies, and complete build commands.
- Exact CLI producing both CSV outputs and the symbol-list format.
- File organization choice; supported venues and symbols.
- Scaling and timestamp policies.
- Instrument-ID derivation and event-type mapping.
- Reconnect, gap, and sequence behavior.
- Brief threading and I/O design note.
- A short sample run, for example one symbol for one to two minutes, with market and book CSVs from the **same window**. Market data may be truncated in the README or attached if small.
- If replay exists, how to run it and the explicit statement that grading replay makes no network calls.
- The GitHub submission section requested at the end of the README.

The PDF's GitHub section instructs initialization/commit, creating a repository, adding an `origin` such as `https://github.com/<your-username>/binance-lob-capture.git`, pushing branch `main`, and tagging/pushing `v1.0.0` with the message `Submission v1.0.0`. These are eventual submission instructions, not actions performed in this stage.

Its suggested `.gitignore` entries are:

```gitignore
build/
*.csv
*.o
*.a
*.so
.env
config/secrets.json
```

It prohibits API keys, private keys, and credentials in the repository and says configuration should use environment variables or a `.env.example` template containing no real values.

### Build, run, and review examples in the PDF

The example prerequisites are Ubuntu 22.04 / Debian, `cmake`, `ninja-build`, `g++-12`, `libssl-dev`, `libboost-all-dev`, and `zlib1g-dev`. The template names GCC 12, C++17, and Clang 15 testing. These are the PDF's examples, not evidence that this workspace has been built or tested.

Example build commands:

```sh
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-12 \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -O2"
cmake --build build --parallel
```

The live examples use `./build/binance_capture` with `--venue`, `--symbols`, and `--output-dir`, including Spot single-symbol, Spot multi-symbol, and USD-M single-symbol cases. The final reviewer checklist gives:

| Step | Command transcribed from the PDF | Expected intent |
| --- | --- | --- |
| Clean build | `cmake -B build && cmake --build build` | Reproducible build from a clean checkout. |
| Live 300-second run | `./build/binance_capture --venue spot --symbols BTCUSDT --duration 300` | Capture and produce both outputs. |
| Book column count | `awk -F',' 'NR==2{print NF}' *_orderbook.csv` | Output `26`. |
| Secrets check | `git grep -i "api_key\|secret\|password"` | The PDF expects empty output. |

For the final row above, the PDF literally prints `git grep -i "api_key|secret|password"` without backslashes or `-E`; Markdown table escaping represents its literal vertical bars. **Caveat:** the printed command does not enable extended-regex alternation and is not a reliable complete secrets check. Also, harmless documentation mentions can match a broader scan. Preserve the intended “no actual secrets” requirement; do not treat that command alone as proof.

Other examples are `head -2` for each CSV, the `awk` count for the sample book file, and these conditional test commands:

```sh
cmake --build build --target tests
./build/tests/unit_tests
```

The PDF's `head -2` example inspects a header and first data row, not the total row count. Its `awk` example checks only the second line, not every row. Those are useful smoke checks; final verification should check the full output.

## 9. Final acceptance and testing checklist

All boxes are intentionally unchecked: this stage documents requirements and does not claim an implementation has passed them.

### A. The PDF's six functional acceptance criteria

- [ ] **F1:** Live capture receives all three required combined streams: `depth@100ms`, `depth5@100ms`, and `trade`.
- [ ] **F2:** Market CSV matches its exact nine-column contract, one-row-per-event policy, and processing order.
- [ ] **F3:** Order-book header matches the complete 26-column schema, and every data row has exactly 26 columns.
- [ ] **F4:** Top-five bids/asks follow correct diff semantics; quantity zero removes a level.
- [ ] **F5:** Scaling and timestamps are documented, and fixed input yields reproducible output.
- [ ] **F6:** Reconnect, exchange-sequence, and connection-epoch behavior is stated and consistent.

### B. Detailed checks derived from the contracts and review criteria

- [ ] URL subscription symbols are lowercase; audit `symbol` values are uppercase.
- [ ] Every supported symbol subscribes to all three streams; trade capture is present even if trade book rows are omitted.
- [ ] Files follow the documented naming/grouping policy and use UTC dates where the date-based naming pattern is selected.
- [ ] Both CSVs are UTF-8 and have their correct header first, with no silent truncation.
- [ ] A CSV-aware parser reads every market row as nine fields, including payloads with quotes, commas, and escaped content.
- [ ] `payload_json` decodes to the minified inner logical object, not the outer envelope.
- [ ] Audit rows reflect exactly the documented ordering seen by book processing.
- [ ] `venue`, `stream_kind`, `shard_id`, `conn_epoch`, and `conn_seq` satisfy their documented contracts.
- [ ] Reconnection increments `conn_epoch` each time; `conn_seq` is monotonic within its specified scope.
- [ ] Book `seqNo` increments once per emitted row according to the documented scope.
- [ ] Instrument IDs are stable for the session and match the documented derivation.
- [ ] `type` is one documented ASCII letter; `side` uses the defined `B`, `S`, or `N` values.
- [ ] Every applied diff and `depth5` produces the required snapshot, including applied events whose visible top five are unchanged.
- [ ] Multiple updates in one event produce the correct final snapshot for that event.
- [ ] Updating an existing level replaces its quantity; adding a new price inserts a level; zero removes it.
- [ ] Best bids sort high to low; best asks low to high; quantities stay paired with their prices.
- [ ] `depth5` follows documented replacement/refresh semantics and does not silently create stale mixed state.
- [ ] Trades do not modify the diff book unless the integration is explicit and justified; optional trade rows follow the documented policy.
- [ ] Price and quantity output fields contain deterministic `int64` values under the documented scales.
- [ ] `recv_tnsec` and `tnsec` remain in `[0, 999999999]`; timestamp sources and source units match documentation.
- [ ] Reconnects and sequence discontinuities trigger the documented reset/resync/drop behavior and consistent CSV output.
- [ ] Parse failures and partial-read paths are handled and logged/counted.
- [ ] Shutdown on `SIGINT` and `SIGTERM` releases socket and I/O resources without dangling references or leaks.
- [ ] Release build, warning flags, compiler, standard and dependencies are documented; unexplained warnings are absent.
- [ ] Parser, allocation, book-update complexity, I/O pressure, and any thread synchronization are reviewable and explained.
- [ ] Observability covers processed messages, errors, reconnects, and written rows as described in the review criteria.
- [ ] Actual secrets are absent; TLS verification and URL/shard limits satisfy the PDF's operational baseline.
- [ ] README provides reproducible build/run commands, symbol format, scaling/time policies, event mappings, recovery behavior, and threading/I/O note.
- [ ] Matching market/book sample outputs cover the same short run and are accessible despite the CSV ignore rule.
- [ ] Final reviewer clean-build and 300-second Spot invocation work, or any explicitly agreed deviation is documented.
- [ ] Eventual submission includes the requested GitHub instructions, ignore rules, repository submission and tag.

### C. Tests that depend on explicit decisions

These cases are useful final checks, but the PDF does not prescribe their exact expected output. Record a policy first; do not turn an assumed outcome into a new assignment requirement.

- [ ] Supported venue scope and product-specific field/sequence semantics have been settled against official references.
- [ ] Startup with no snapshot, an empty book, or fewer than five levels has a documented, verified output policy.
- [ ] Missing-level padding preserves the 26-column schema and documented field interpretation.
- [ ] Duplicate/stale/out-of-order diffs and removal of absent levels behave according to the chosen policy.
- [ ] A top-five level disappearing exposes a valid known next level or triggers the documented incomplete-state behavior.
- [ ] Interleaved diff/`depth5` events and stale refreshes follow a clearly documented ordering/reconciliation policy.
- [ ] Audit retention of rejected events is consistent with replay, counters, and the chosen interpretation of row-per-event.
- [ ] Missing event-time fields, equal timestamps, and wall-clock changes follow the documented time/order rules.
- [ ] Excess decimal precision and integer overflow are handled deterministically without silent data corruption.
- [ ] Repeated runs, reconnects, date rollover and file reopening follow documented sequence/file/header policies.
- [ ] Slow disk, unavailable network and invalid configuration follow the documented error/limit behavior.

### D. Conditional checks for optional work

- [ ] **If replay is implemented:** regenerate the book from saved market CSV, with no network calls during grading replay, and document that guarantee.
- [ ] **If REST resynchronization is implemented:** validate snapshot/diff alignment using the official product rules and explain how replay obtains any necessary initialization state without network access.
- [ ] **If multi-symbol/sharding is implemented:** verify symbol isolation, instrument IDs, shard/epoch sequencing, ordering, and output grouping.
- [ ] **If trade book rows are implemented:** verify the documented annotation/integration, including avoidance of unexplained double counting.
- [ ] **If multithreading is implemented:** validate ownership and synchronization, including shutdown and back-pressure.
- [ ] **If tests are supplied:** document runnable commands; the PDF's test-target examples are conditional.
- [ ] **If performance measurements or extra compiler claims are supplied:** record actual measured/tested evidence rather than copying template claims.

**Stage boundary:** requirements review ends here. Architecture, C++ implementation, live capture, repository publication, and release tagging await the user's approval to proceed to the relevant stage.
