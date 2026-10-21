#include "config.hpp"
#include "csv_reader.hpp"
#include "replay.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

using namespace binance_capture;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Action> std::string rejects(Action action, const char* message) {
    try { action(); }
    catch (const std::exception& error) { ++checks; return error.what(); }
    throw std::runtime_error(message);
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "cannot read fixture/output");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
Config arguments(std::vector<std::string> values) {
    std::vector<char*> argv;
    for (auto& value : values) argv.push_back(value.data());
    return parse_arguments(static_cast<int>(argv.size()),argv.data());
}
void parser_tests() {
    for (const std::string ending : {"\r\n", "\n", ""}) {
        std::istringstream input("a,\"b,c\",\"d\"\"e\",\"line1\r\nline2\",," + ending);
        std::vector<std::string> row;
        require(read_csv_record(input,row), "valid quoted CSV record");
        require(row == std::vector<std::string>{"a","b,c","d\"e","line1\r\nline2","",""}, "decoded quoted fields");
        require(!read_csv_record(input,row), "EOF after record");
    }
    for (const std::string invalid : {"\"unfinished", "a\"b\n", "\"x\"trailing\n", "a\rb", "a\r",
                                      "1,2,3,4,5,6,7,8,9,10\n"}) {
        std::istringstream input(invalid); std::vector<std::string> row;
        rejects([&] { read_csv_record(input,row); }, "invalid CSV accepted");
    }
    std::istringstream nul(std::string("a\0b\n",4)); std::vector<std::string> row;
    rejects([&] { read_csv_record(nul,row); }, "NUL accepted");
    std::istringstream bad; bad.setstate(std::ios::badbit);
    rejects([&] { read_csv_record(bad,row); }, "input read error accepted");
    std::istringstream boundary(std::string(max_csv_record_bytes-2,'x')+"\r\n");
    require(read_csv_record(boundary,row) && row[0].size() == max_csv_record_bytes-2, "record bound exact");
    std::istringstream huge(std::string(max_csv_record_bytes+1,'x'));
    rejects([&] { read_csv_record(huge,row); }, "record byte bound missing");
    std::cout << "PASS bounded CSV parsing, quotes, embedded CRLF, EOF and read errors\n";
}
void reliability_tests(const std::filesystem::path& root) {
    const DepthEvent short_snapshot{DepthKind::snapshot,10,10,{{100,1}},{{102,1}}};
    OrderBook short_book;
    require(short_book.apply(short_snapshot) == BookDecision::applied_snapshot && !short_book.ready(), "short anchor");
    require(short_book.apply(short_snapshot) == BookDecision::applied_snapshot, "matching equal short refresh");
    auto contradictory = short_snapshot; contradictory.bids[0].quantity = 2;
    rejects([&] { short_book.apply(contradictory); }, "contradictory short snapshot was silently replaced");
    require(!short_book.ready() && short_book.top().bids[0].price == 0, "short contradiction clears state");
    OrderBook bound;
    bound.apply({DepthKind::snapshot,1,1,{{100000,1},{99000,1},{98000,1},{97000,1},{96000,1}},
        {{200000,1},{201000,1},{202000,1},{203000,1},{204000,1}}});
    DepthEvent first{DepthKind::diff,2,2,{},{}}, second{DepthKind::diff,3,3,{},{}};
    for (std::int64_t i=0; i<10000; ++i) {
        first.bids.push_back({110000+i,1}); second.bids.push_back({120000+i,1});
    }
    require(bound.apply(first) == BookDecision::applied_diff, "first retained-level batch");
    rejects([&] { bound.apply(second); }, "retained-level limit missing");
    require(!bound.ready(), "level limit invalidates state");
    OrderBookCsv output(root/"failure_state.csv"); BookCounters counters{}; OrderBook book;
    auto anchor = parse_market_payload(StreamKind::depth5,"BTCUSDT",
        R"({"lastUpdateId":1,"bids":[["100","1"],["99","1"],["98","1"],["97","1"],["96","1"]],"asks":[["102","1"],["103","1"],["104","1"],["105","1"],["106","1"]]})");
    process_book_event({{1,1},1},anchor,book,output,counters);
    require(book.ready(), "pipeline initially ready");
    auto malformed = parse_market_payload(StreamKind::depth_diff,"BTCUSDT",R"({"U":2,"u":2})");
    rejects([&] { process_book_event({{1,2},2},malformed,book,output,counters); }, "malformed event accepted");
    require(!book.ready() && output.rows() == 1, "semantic failure invalidates without extra output");
    output.close();
    std::cout << "PASS short equal-revision contradictions, retained-level bound and fail-closed pipeline\n";
}
void fixture_tests(const std::filesystem::path& fixtures, const std::filesystem::path& root) {
    const auto expected = read_file(fixtures/"stage4e_expected_orderbook.csv");
    for (int run=1; run<=2; ++run) {
        std::ifstream input(fixtures/"stage4e_market.csv",std::ios::binary);
        const auto path = root/("fixture-"+std::to_string(run)+".csv");
        OrderBookCsv output(path);
        const auto stats = replay_market_csv(input,output);
        require(stats.market_rows == 27 && output.rows() == 15, "fixture row counts");
        const BookCounters expected_counts{9,6,2,1,4,1,1,1};
        require(stats.decisions == expected_counts, "independent decision counts");
        require(read_file(path) == expected, "fixture differs from hand-calculated expected bytes");
    }
    require(read_file(root/"fixture-1.csv") == read_file(root/"fixture-2.csv"), "replay not deterministic");
    rejects([&] { OrderBookCsv duplicate(root/"fixture-1.csv"); }, "replay output overwritten");
    std::cout << "PASS 27-event fixture, 15 expected rows, all decisions and byte-identical repeat\n";
}
std::string tape_row(const std::vector<std::string>& row) {
    std::string result;
    for (const auto& value : row) {
        if (!result.empty()) result += ',';
        result += csv_escape(value);
    }
    return result+"\r\n";
}
void malformed_tests(const std::filesystem::path& fixtures, const std::filesystem::path& root) {
    std::ifstream source(fixtures/"stage4e_market.csv",std::ios::binary);
    std::vector<std::string> header, skipped, anchor;
    read_csv_record(source,header); read_csv_record(source,skipped); read_csv_record(source,anchor);
    int index=0;
    const auto reject_tape = [&](const std::string& tape, const std::string& context) {
        std::istringstream input(tape);
        OrderBookCsv output(root/("invalid-"+std::to_string(++index)+".csv"));
        const auto error = rejects([&] { replay_market_csv(input,output); }, "malformed replay accepted");
        require(error.find(context) != std::string::npos, "error missing record/reason context");
    };
    reject_tape("", "record 1");
    auto wrong_header=header; std::swap(wrong_header[0],wrong_header[1]);
    reject_tape(tape_row(wrong_header), "header mismatch");
    for (const auto& change : std::vector<std::pair<std::size_t,std::string>>{
        {0,"9223372036854775808"},{0,"+1"},{1,"-1"},{1,"1000000000"},{1,"1.2"},
        {2,"usdm"},{3,"unexpected"},{4,"1"},{5,"-1"},{5,"18446744073709551616"},
        {6,"0"},{6,"-1"},{6,"1.0"},{7,"ETHUSDT"},{8,"{"},{8,"[]"},
        {8," {\"lastUpdateId\":1}"},{8,R"({"s":"ETHUSDT"})"},
        {8,R"({"lastUpdateId":1,"bids":[["100","0.000000001"]],"asks":[]})"},
        {8,R"({"lastUpdateId":1,"bids":[["100","92233720369"]],"asks":[]})"}}) {
        auto row=anchor; row[change.first]=change.second;
        reject_tape(tape_row(header)+tape_row(row), "record 2");
    }
    auto missing=anchor; missing.pop_back();
    reject_tape(tape_row(header)+tape_row(missing), "nine");
    auto extra=anchor; extra.push_back("extra");
    reject_tape(tape_row(header)+tape_row(extra), "nine");
    reject_tape(tape_row(header)+"\"unterminated", "record 2");
    reject_tape(tape_row(header)+tape_row(anchor)+tape_row(anchor), "record 3");
    auto backward=anchor; backward[6]="1";
    reject_tape(tape_row(header)+tape_row(anchor)+tape_row(backward), "record 3");
    auto newer=anchor; newer[5]="1"; newer[6]="1";
    auto old_epoch=anchor; old_epoch[6]="100";
    reject_tape(tape_row(header)+tape_row(newer)+tape_row(old_epoch), "record 3");
    auto malformed=anchor; malformed[6]="3"; malformed[8]=R"({"lastUpdateId":101,"bids":[["100","-1"]],"asks":[]})";
    const auto prefix_path=root/"prefix.csv";
    std::istringstream bad_input(tape_row(header)+tape_row(anchor)+tape_row(malformed));
    OrderBookCsv prefix(prefix_path);
    rejects([&] { replay_market_csv(bad_input,prefix); }, "semantic replay error accepted");
    const auto expected=read_file(fixtures/"stage4e_expected_orderbook.csv");
    const auto second_end=expected.find("\r\n",expected.find("\r\n")+2)+2;
    require(read_file(prefix_path) == expected.substr(0,second_end), "error did not preserve correct book prefix");
    std::istringstream header_only(tape_row(header)); OrderBookCsv empty(root/"empty.csv");
    require(replay_market_csv(header_only,empty).market_rows == 0 && empty.rows() == 0, "header-only audit policy");
    std::istringstream valid(tape_row(header)+tape_row(anchor)); OrderBookCsv closed(root/"closed.csv");
    closed.close();
    const auto io_error=rejects([&] { replay_market_csv(valid,closed); }, "replay write failure ignored");
    require(io_error.find("record 2") != std::string::npos && closed.rows() == 0, "replay write error context/counter");
    std::cout << "PASS malformed headers, metadata, sequencing, JSON, decimals and preserved error prefix\n";
}
void cli_tests() {
    const auto replay=arguments({"app","--replay","fixture.csv","--output-dir","out"});
    require(replay.replay_file == "fixture.csv" && replay.output_dir == "out", "replay CLI");
    const auto live=arguments({"app","--venue","spot","--symbols","BTCUSDT","--duration","60"});
    require(live.replay_file.empty() && live.duration_seconds == 60, "live CLI changed");
    for (const auto& values : std::vector<std::vector<std::string>>{
        {"app","--replay"},{"app","--replay",""},{"app","--replay","x","--replay","y"},
        {"app","--replay","x","--duration","1"},{"app","--duration","1","--replay","x"},
        {"app","--replay","x","--venue","usdm"},{"app","--help","--unknown"}}) {
        rejects([&] { arguments(values); }, "invalid CLI accepted");
    }
    std::cout << "PASS replay/live CLI selection and incompatible options\n";
}
}
int main(int argc,char* argv[]) {
    try {
        require(argc == 2,"fixture directory argument required");
        const auto root=std::filesystem::temp_directory_path()/("binance-replay-test-"
            +std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        parser_tests(); reliability_tests(root); fixture_tests(argv[1],root); malformed_tests(argv[1],root); cli_tests();
        // Only the files created directly by this test are removed.
        for (const auto& entry : std::filesystem::directory_iterator(root)) std::filesystem::remove(entry.path());
        std::filesystem::remove(root);
        std::cout << "Passed " << checks << " replay/reliability checks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Replay test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
