#include "replay.hpp"
#include "csv_reader.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace binance_capture {
namespace {
template<class Integer> Integer parse_integer(const std::string& text, const char* name) {
    Integer value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument(std::string("invalid integer field: ") + name);
    }
    return value;
}
StreamKind read_kind(const std::string& text) {
    if (text == "depth_diff") return StreamKind::depth_diff;
    if (text == "depth5") return StreamKind::depth5;
    if (text == "trade") return StreamKind::trade;
    throw std::invalid_argument("unexpected stream_kind: " + text);
}
EventMetadata read_metadata(const std::vector<std::string>& row) {
    if (row.size() != 9) throw std::invalid_argument("expected exactly nine CSV fields");
    if (row[2] != "spot" || row[4] != "0" || row[7] != "BTCUSDT") {
        throw std::invalid_argument("replay supports only spot, shard 0, BTCUSDT");
    }
    EventMetadata metadata{{parse_integer<std::int64_t>(row[0], "recv_tsec"),
                            parse_integer<std::int32_t>(row[1], "recv_tnsec")},
                           parse_integer<std::uint64_t>(row[6], "conn_seq"),
                           parse_integer<std::uint64_t>(row[5], "conn_epoch")};
    if (metadata.received.nanoseconds < 0 || metadata.received.nanoseconds >= 1000000000
        || metadata.conn_seq == 0) throw std::invalid_argument("invalid timestamp remainder or zero conn_seq");
    return metadata;
}
}

ReplayStats replay_market_csv(std::istream& input, OrderBookCsv& output) {
    ReplayStats stats;
    OrderBook book;
    std::uint64_t record = 1;
    try {
        std::vector<std::string> row, header;
        std::istringstream expected(std::string(market_header) + '\n');
        read_csv_record(expected, header);
        if (!read_csv_record(input, row) || row != header) throw std::invalid_argument("market CSV header mismatch");
        bool have_previous = false;
        std::uint64_t previous_epoch = 0, previous_sequence = 0;
        for (;;) {
            ++record;
            if (!read_csv_record(input, row)) break;
            const auto metadata = read_metadata(row);
            if (have_previous && (metadata.conn_epoch < previous_epoch
                || (metadata.conn_epoch == previous_epoch && metadata.conn_seq <= previous_sequence))) {
                throw std::invalid_argument("conn_epoch or conn_seq moved backwards/duplicated");
            }
            const auto event = parse_market_payload(read_kind(row[3]), row[7], row[8]);
            if (event.payload_json != row[8]) throw std::invalid_argument("payload_json must be minified inner JSON");
            process_book_event(metadata, event, book, output, stats.decisions);
            ++stats.market_rows;
            have_previous = true;
            previous_epoch = metadata.conn_epoch;
            previous_sequence = metadata.conn_seq;
        }
        output.close();
        return stats;
    } catch (const std::exception& error) {
        book.invalidate();
        // Preserve the original failing record even if finalization also fails.
        const std::string reason = error.what();
        try { output.close(); }
        catch (const std::exception& close_error) { std::cerr << "Replay finalization error: " << close_error.what() << '\n'; }
        throw std::runtime_error("replay record " + std::to_string(record) + ": " + reason);
    }
}

int run_replay(const std::filesystem::path& input_path, const std::filesystem::path& output_directory) {
    std::ifstream input(input_path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open replay input: " + input_path.u8string());
    const auto output_path = output_directory / order_book_path(input_path).filename();
    OrderBookCsv output(output_path);
    const auto stats = replay_market_csv(input, output);
    std::cerr << "Replay market_rows=" << stats.market_rows << " book_rows=" << output.rows();
    for (std::size_t i = 0; i < stats.decisions.size(); ++i) {
        std::cerr << ' ' << decision_name(static_cast<BookDecision>(i)) << '=' << stats.decisions[i];
    }
    std::cerr << "\nOrder-book CSV: " << output_path.u8string() << '\n';
    return 0;
}
}
