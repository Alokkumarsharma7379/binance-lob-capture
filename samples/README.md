# Genuine Binance capture excerpt

These files are unchanged, contiguous byte prefixes of the successful 60-second Binance Spot BTCUSDT capture from October 21, 2026 at 09:35:03 UTC / 15:05:03 IST. They cover the same initial interval: receive timestamps `1792575304.643604200` through `1792575306.604636600` (1.9610324 seconds). This is an excerpt, not a claim that these small files contain the entire minute.

| File | Data rows | Contents |
| --- | ---: | --- |
| [market_data_spot_BTCUSDT_2026-10-21.csv](market_data_spot_BTCUSDT_2026-10-21.csv) | 225 | Every event from connection sequence 1 through 225: 20 depth5, 20 diffs, 185 trades. |
| [market_data_spot_BTCUSDT_2026-10-21_orderbook.csv](market_data_spot_BTCUSDT_2026-10-21_orderbook.csv) | 20 | All emitted book rows triggered by that market prefix. |

No row within the selected prefix was omitted or altered. The book sequence remains 1–20. Both files retain the original exact headers, UTF-8 and CRLF bytes; Git attributes prevent line-ending conversion. Offline replay of the market excerpt must reproduce the paired book file byte for byte.

All book rows are `P`: each of the 20 diffs followed a depth5 snapshot with the same final exchange update ID, so it was already covered. The offline test fixture under `tests/fixtures/` separately demonstrates uncovered diffs producing `D` rows; it is clearly synthetic test data and is not presented as live output.

The full successful run exited 0 after 60,403 ms: 2,431 market rows (590 diffs, 590 depth5, 1,251 trades), 590 `P` book rows, zero fatal errors and one reported TLS shutdown warning. A separate 120-second attempt failed with an idle timeout; it is not the source of these samples. Details and limitations: [final validation](../docs/stage4f-final-validation.md).

SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| This market excerpt | `21ad55da9361bbcf0596fd6a32aea142c9dd038e9b5f46c3d25b8515df168dac` |
| This book excerpt | `eb3c3698757dae1c3253121de271497e2ba715e46cde2f211d268ebec15864c8` |
| Original full market file, retained locally under ignored `output/` | `f0f1a2084cad1ec3d567656729f68ca7bdf2a04ab908162a2bc1ed5b2678f4cc` |
| Original full book file, retained locally under ignored `output/` | `fd1723ffbde87d1b032b581aa9465f8660766bb9a2e2ea243fafd64ac921d068` |

From a Linux build, reproduce and compare using a fresh output directory:

```sh
./build-linux/binance_capture --replay samples/market_data_spot_BTCUSDT_2026-10-21.csv --output-dir ./output/sample-replay
cmp samples/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv output/sample-replay/market_data_spot_BTCUSDT_2026-10-21_orderbook.csv
```

The corresponding Windows command uses `.\build-windows\binance_capture.exe`; compare with `Get-FileHash` or `cmake -E compare_files`. Large generated captures remain ignored. Only this explicit matching sample pair and the test fixtures are allowed by `.gitignore`.
