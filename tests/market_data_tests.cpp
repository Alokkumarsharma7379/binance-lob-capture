#include "market_data.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Action>
void rejects(Action action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}

// Independent quoted-field reader: checks round trips rather than counting commas.
std::vector<std::vector<std::string>> read_csv(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, ended_quote = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (quoted) {
            if (character == '"') {
                if (index + 1 < text.size() && text[index + 1] == '"') {
                    field += '"';
                    ++index;
                } else {
                    quoted = false;
                    ended_quote = true;
                }
            } else field += character;
        } else if (character == ',' || character == '\r') {
            row.push_back(field);
            field.clear();
            ended_quote = false;
            if (character == '\r') {
                require(index + 1 < text.size() && text[++index] == '\n', "expected CRLF");
                rows.push_back(row);
                row.clear();
            }
        } else if (character == '"' && field.empty() && !ended_quote) {
            quoted = true;
        } else {
            require(!ended_quote && character != '\n' && character != '"', "invalid CSV quoting");
            field += character;
        }
    }
    require(!quoted && row.empty() && field.empty() && !ended_quote, "unfinished CSV record");
    return rows;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "cannot read fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    using namespace binance_capture;
    using namespace std::chrono;
    try {
        const auto origin = system_clock::time_point{};
        const auto stamp = receive_timestamp(origin + seconds(1700000000) + nanoseconds(123456700));
        require(stamp.seconds == 1700000000 && stamp.nanoseconds == 123456700, "timestamp split");
        const auto before = receive_timestamp(origin - nanoseconds(100));
        require(before.seconds == -1 && before.nanoseconds == 999999900, "negative timestamp split");
        const auto boundary = receive_timestamp(origin + seconds(1));
        require(boundary.seconds == 1 && boundary.nanoseconds == 0, "timestamp boundary");

        const nlohmann::json payload = {
            {"s", "BTCUSDT"}, {"p", "101.23000000"}, {"q", "0.00100000"},
            {"note", "comma, quote \" slash \\ CR\r LF\n tab\t"},
            {"extra", {{"flag", true}, {"id", std::uint64_t{9007199254740993ULL}}}},
            {"utf8", "\xE2\x82\xAC"}
        };
        const auto outer = nlohmann::json{{"stream", "btcusdt@trade"}, {"data", payload}}.dump(2);
        const auto event = parse_envelope(outer);
        require(event.kind == StreamKind::trade && event.symbol == "BTCUSDT", "classification/symbol");
        require(nlohmann::json::parse(event.payload_json) == payload, "inner JSON fields changed");
        require(event.payload_json.find('\n') == std::string::npos, "payload not one line");
        require(event.payload_json.find("101.23000000") != std::string::npos, "decimal string changed");
        require(!nlohmann::json::parse(event.payload_json).contains("data"), "outer envelope persisted");
        rejects([] { parse_envelope(R"({"stream":"btcusdt@trade","data":{"s":"ETHUSDT"}})"); },
                "wrong payload symbol accepted");
        rejects([] { parse_envelope(R"({"stream":"btcusdt@trade","data":{"s":12}})"); },
                "numeric symbol accepted");
        rejects([&] { parse_envelope(outer, "ETHUSDT"); }, "unsupported configured symbol accepted");
        require(parse_envelope(R"({"stream":"btcusdt@depth5@100ms","data":{"lastUpdateId":42}})").symbol
                == "BTCUSDT", "depth5 symbol must come from subscription");

        const std::vector<std::string> fields = {"plain", "a,b", "a\"b", "a\rb", "a\nb", ""};
        for (const auto& field : fields) {
            require(read_csv(csv_escape(field) + "\r\n")[0][0] == field, "escaping round trip");
        }
        std::uint64_t sequence = 0;
        std::string tape = std::string(market_header) + "\r\n";
        for (int index = 0; index < 3; ++index) {
            tape += format_market_row({stamp, next_connection_sequence(sequence)}, event);
        }
        const auto records = read_csv(tape);
        require(records.size() == 4 && records[0].size() == 9, "header/row count");
        const std::vector<std::string> expected_header = {"recv_tsec", "recv_tnsec", "venue", "stream_kind",
            "shard_id", "conn_epoch", "conn_seq", "symbol", "payload_json"};
        require(records[0] == expected_header, "exact header mismatch");
        for (std::size_t index = 1; index < records.size(); ++index) {
            const auto& row = records[index];
            require(row.size() == 9 && row[0] == "1700000000" && row[1] == "123456700", "row/time schema");
            require(row[2] == "spot" && row[3] == "trade" && row[4] == "0" && row[5] == "0", "metadata");
            require(row[6] == std::to_string(index) && row[7] == "BTCUSDT", "sequence/symbol");
            require(nlohmann::json::parse(row[8]) == payload, "CSV JSON round trip");
        }
        next_connection_sequence(sequence); // A nonmarket text message consumes a sequence too.
        require(read_csv(format_market_row({stamp, next_connection_sequence(sequence)}, event))[0][6] == "5",
                "control-message sequence gap");
        sequence = std::numeric_limits<std::uint64_t>::max();
        rejects([&] { next_connection_sequence(sequence); }, "sequence overflow accepted");
        require(sequence == std::numeric_limits<std::uint64_t>::max(), "overflow wrapped sequence");
        rejects([&] { format_market_row({{0, 1000000000}, 1}, event); }, "bad nanoseconds accepted");
        rejects([&] { format_market_row({stamp, 0}, event); }, "zero sequence accepted");
        rejects([&] { format_market_row({stamp, 1}, {StreamKind::server_shutdown, "BTCUSDT", "{}"}); },
                "control notice written as market data");

        const auto root = std::filesystem::temp_directory_path()
            / ("binance-csv-test-" + std::to_string(steady_clock::now().time_since_epoch().count()));
        const auto path = market_data_path(root / "nested", origin);
        require(path.filename() == "market_data_spot_BTCUSDT_1970-01-01.csv", "UTC filename");
        {
            MarketDataCsv csv(path);
            require(read_file(path) == std::string(market_header) + "\r\n", "header not written once");
            csv.write({stamp, 1}, event);
            csv.write({stamp, 2}, event);
            csv.flush();
            require(csv.rows() == 2, "row counter");
            csv.close();
            csv.close();
            rejects([&] { csv.write({stamp, 3}, event); }, "closed write accepted");
            rejects([&] { csv.flush(); }, "closed flush accepted");
        }
        const auto saved = read_file(path);
        require(read_csv(saved).size() == 3, "file rows/header");
        rejects([&] { MarketDataCsv duplicate(path); }, "existing file overwritten");
        require(read_file(path) == saved, "overwrite rejection altered original");
        rejects([&] { MarketDataCsv blocked(path / "child.csv"); }, "file used as directory");
        rejects([&] { MarketDataCsv directory(path.parent_path()); }, "directory opened as output file");

#ifdef _WIN32
        auto* read_only = _wfopen(path.c_str(), L"rb");
#else
        auto* read_only = std::fopen(path.c_str(), "rb");
#endif
        require(read_only != nullptr, "readonly fixture open");
        rejects([&] { write_csv_bytes(read_only, "cannot write"); }, "failed write not reported");
        rejects([&] { flush_csv_file(read_only); }, "stream error not reported at flush");
        std::fclose(read_only);
        std::filesystem::remove(path);
        std::filesystem::remove(root / "nested");
        std::filesystem::remove(root);
        std::cout << "Passed CSV round trips, inner JSON preservation, metadata, timestamps, sequences, "
                     "exclusive file creation and file-error checks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Market-data test failed: " << error.what() << '\n';
        return 1;
    }
}
