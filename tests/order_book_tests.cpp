#include "depth_event.hpp"
#include "event_processing.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace binance_capture;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Action> void rejects(Action action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
DepthEvent anchor(std::uint64_t id = 100) {
    return {DepthKind::snapshot, id, id,
        {{100,10},{99,9},{98,8},{97,7},{96,6}},
        {{102,12},{103,13},{104,14},{105,15},{106,16}}};
}
DepthEvent diff(std::uint64_t first, std::uint64_t last,
                std::vector<Level> bids = {}, std::vector<Level> asks = {}) {
    return {DepthKind::diff, first, last, std::move(bids), std::move(asks)};
}
MarketEvent wire(const DepthEvent& depth) {
    nlohmann::ordered_json data;
    if (depth.kind == DepthKind::snapshot) data["lastUpdateId"] = depth.last;
    else {
        data = {{"e","depthUpdate"},{"E",1700000000000ULL},{"s","BTCUSDT"},
                {"U",depth.first},{"u",depth.last}};
    }
    auto bids = nlohmann::ordered_json::array(), asks = bids;
    for (const auto& x : depth.bids) bids.push_back({std::to_string(x.price),std::to_string(x.quantity)});
    for (const auto& x : depth.asks) asks.push_back({std::to_string(x.price),std::to_string(x.quantity)});
    const bool snapshot = depth.kind == DepthKind::snapshot;
    data[snapshot ? "bids" : "b"] = bids;
    data[snapshot ? "asks" : "a"] = asks;
    return parse_envelope(nlohmann::ordered_json{{"stream",snapshot ? "btcusdt@depth5@100ms"
        : "btcusdt@depth@100ms"},{"data",data}}.dump());
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "cannot read test CSV");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void operations() {
    OrderBook book;
    require(book.apply(anchor()) == BookDecision::applied_snapshot && book.ready(), "anchor");
    require(book.top().bids[0] == Level{100,10} && book.top().asks[4] == Level{106,16}, "initial order");
    require(book.apply(diff(101,101,{{100,5}})) == BookDecision::applied_diff, "replace");
    require(book.top().bids[0] == Level{100,5}, "absolute quantity rather than delta");
    require(book.apply(diff(102,102,{{101,2}},{{102,3}})) == BookDecision::applied_diff, "multi-side insertion");
    require(book.top().bids[0] == Level{101,2} && book.top().bids[4] == Level{97,7}
        && book.top().asks[0] == Level{102,3}, "best five order");
    require(book.apply(diff(103,103,{{101,0},{999,0}})) == BookDecision::applied_diff, "delete/nonexistent deletion");
    require(book.top().bids[4] == Level{96,6}, "known sixth promoted");
    require(book.apply(diff(104,104,{},{{101,1}})) == BookDecision::applied_diff, "ask insertion");
    require(book.top().asks[0] == Level{101,1} && book.top().asks[4] == Level{105,15}, "ask ascending");
    require(book.apply(diff(105,105,{},{{101,0}})) == BookDecision::applied_diff, "ask deletion");
    require(book.top().asks[4] == Level{106,16}, "known ask sixth");
    auto replacement = anchor(106);
    replacement.bids[0].quantity = 88;
    require(book.apply(replacement) == BookDecision::applied_snapshot, "fresh replacement");
    require(book.top().bids[0] == Level{100,88}, "replacement quantity");
    require(book.apply(diff(107,107,{{95,100}})) == BookDecision::applied_diff, "outside-only accepted");
    require(book.apply(diff(108,108,{{96,0}})) == BookDecision::coverage_lost, "unknown sixth despite outside update");
    require(!book.ready() && book.top().bids[0].price == 0, "lost coverage invalidates maps");
    require(book.apply(anchor(107)) == BookDecision::stale_snapshot, "recovery high watermark");
    require(book.apply(anchor(108)) == BookDecision::applied_snapshot && book.ready(), "fresh recovery");
    OrderBook replaced;
    replaced.apply(anchor());
    replaced.apply(diff(101,101,{{101,1}}));
    auto new_anchor = anchor(102);
    new_anchor.bids = {{101,1},{100,10},{99,9},{98,8},{97,7}};
    require(replaced.apply(new_anchor) == BookDecision::applied_snapshot, "replacement with changed boundary");
    require(replaced.apply(diff(103,103,{{101,0}})) == BookDecision::coverage_lost,
        "replacement must discard old sixth outside new coverage");
    std::cout << "PASS map operations, sorting, replacement and known/unknown sixth\n";
}
void sequencing() {
    OrderBook book;
    require(book.apply(diff(98,100)) == BookDecision::waiting, "wait before anchor");
    require(book.apply(anchor(99)) == BookDecision::stale_snapshot, "pre-anchor high watermark");
    book.apply(anchor());
    require(book.apply(diff(98,101,{{100,20}})) == BookDecision::applied_diff, "overlapping U/u");
    require(book.apply(diff(98,101,{{100,20}})) == BookDecision::stale_diff, "duplicate");
    require(book.apply(diff(90,99,{{100,55}})) == BookDecision::stale_diff, "old diff cannot mutate");
    require(book.top().bids[0].quantity == 20, "stale left quantity intact");
    auto equal = anchor(101); equal.bids[0].quantity = 20;
    require(book.apply(equal) == BookDecision::applied_snapshot, "equal consistent snapshot emits");
    require(book.consistency_checks() == 2, "same revision checks");
    require(book.apply(diff(103,104)) == BookDecision::gap && !book.ready(), "gap detection");
    require(book.apply(diff(102,103)) == BookDecision::waiting, "late missing update cannot restart");
    require(book.apply(anchor(103)) == BookDecision::stale_snapshot, "gap recovery watermark");
    book.apply(anchor(104));
    book.observe_epoch(1);
    require(!book.ready() && book.revision() == 0, "epoch invalidation");
    require(book.apply(diff(1,2),1) == BookDecision::waiting, "new epoch waits");
    require(book.apply(anchor(2),1) == BookDecision::applied_snapshot, "new epoch IDs restart");
    rejects([&] { book.observe_epoch(0); }, "backward epoch");
    OrderBook conflict;
    conflict.apply(anchor());
    rejects([&] { conflict.apply(diff(99,100,{{100,11}})); }, "same-ID diff contradiction");
    require(!conflict.ready(), "contradiction invalidates");
    conflict.apply(anchor(101));
    auto wrong = anchor(101); wrong.asks[0].quantity = 99;
    rejects([&] { conflict.apply(wrong); }, "same-ID snapshot contradiction");
    OrderBook max;
    max.apply(anchor(std::numeric_limits<std::uint64_t>::max()));
    require(max.apply(diff(1,std::numeric_limits<std::uint64_t>::max())) == BookDecision::stale_diff,
        "max uint64 sequence does not wrap");
    std::cout << "PASS continuity, overlap, stale/equal IDs, gaps, epoch and uint64 boundary\n";
}
void invalid_and_short() {
    OrderBook book;
    book.apply(anchor());
    const auto before = book.top();
    rejects([&] { book.apply(diff(101,101,{{100,2}},{{103,-1}})); }, "invalid second side");
    require(book.top() == before && book.revision() == 100, "whole message validation before mutation");
    rejects([&] { book.apply(diff(102,101)); }, "U greater than u");
    rejects([&] { book.apply(diff(101,101,{{100,2},{100,3}})); }, "duplicate price");
    rejects([&] { book.apply(diff(101,101,{{0,2}})); }, "zero price");
    auto bad = anchor(101); std::swap(bad.bids[0],bad.bids[1]);
    rejects([&] { book.apply(bad); }, "unsorted snapshot");
    bad = anchor(101); bad.asks[0].quantity = 0;
    rejects([&] { book.apply(bad); }, "zero snapshot size");
    bad = anchor(101); bad.bids[0].price = 102;
    rejects([&] { book.apply(bad); }, "crossed snapshot");
    bad = anchor(101); bad.bids.push_back({95,1});
    rejects([&] { book.apply(bad); }, "six snapshot levels");
    require(book.top() == before, "invalid snapshots left state intact");
    require(book.apply(diff(101,101,{{102,1}})) == BookDecision::crossed && !book.ready(), "crossed diff invalidates");
    auto short_snapshot = anchor(102); short_snapshot.bids.resize(2);
    require(book.apply(short_snapshot) == BookDecision::applied_snapshot && !book.ready(), "short snapshot exact output");
    require(book.top().bids[1] == Level{99,9} && book.top().bids[2] == Level{}, "short zero pairs");
    require(book.apply(diff(103,103)) == BookDecision::waiting, "short snapshot not diff-ready");
    OrderBook limit;
    limit.apply(anchor());
    auto huge = diff(101,101); huge.bids.assign(10001,{100,1});
    rejects([&] { limit.apply(huge); }, "change limit");
    std::cout << "PASS validation atomicity, crossed state, short snapshots and bounds\n";
}
void decoding() {
    const auto event = wire(anchor());
    const auto depth = decode_depth_event(event);
    require(depth.bids[0] == Level{10000000000LL,1000000000LL}, "decimal decode scaling");
    require(depth.last == 100 && depth.kind == DepthKind::snapshot, "snapshot fields");
    const auto valid = wire(diff(101,102,{{100,0}},{{103,2}}));
    require(decode_depth_event(valid).bids[0].quantity == 0, "zero decoded");
    for (const auto& key : {"U","u","E","b","a","e","s"}) {
        auto broken = valid; broken.data.erase(key);
        rejects([&] { decode_depth_event(broken); }, "missing required diff field");
    }
    for (const auto& value : {nlohmann::ordered_json(-1),nlohmann::ordered_json(1.2),
                            nlohmann::ordered_json("101"),nlohmann::ordered_json(true)}) {
        auto broken = valid; broken.data["U"] = value;
        rejects([&] { decode_depth_event(broken); }, "invalid ID type");
    }
    auto broken = valid; broken.data["b"] = {{100,"1"}};
    rejects([&] { decode_depth_event(broken); }, "numeric price");
    broken = valid; broken.data["a"] = {{"103","0.000000001"}};
    rejects([&] { decode_depth_event(broken); }, "excess precision");
    broken = event; broken.data["lastUpdateId"] = 18446744073709551615ULL;
    require(decode_depth_event(broken).last == std::numeric_limits<std::uint64_t>::max(), "large integer exactness");
    std::cout << "PASS decoded JSON types, required fields, exact scaling and IDs\n";
}
void deeper_reference() {
    // Independent six-level fixture: level 95 exists but is never sent by the anchor.
    std::vector<Level> deeper{{100,10},{99,9},{98,8},{97,7},{96,6},{95,5}};
    OrderBook book; book.apply(anchor());
    deeper.insert(deeper.begin(),{101,1});
    book.apply(diff(101,101,{{101,1}}));
    for (std::size_t i=0; i<5; ++i) require(book.top().bids[i] == deeper[i], "deeper reference insertion");
    deeper.erase(deeper.begin());
    book.apply(diff(102,102,{{101,0}}));
    for (std::size_t i=0; i<5; ++i) require(book.top().bids[i] == deeper[i], "deeper reference promotion");
    deeper.erase(deeper.begin()+4);
    require(deeper[4] == Level{95,5}, "reference has unchanged sixth");
    require(book.apply(diff(103,103,{{96,0}})) == BookDecision::coverage_lost, "unknown reference sixth must not be guessed");
    auto fresh = anchor(103); fresh.bids.assign(deeper.begin(),deeper.begin()+5);
    book.apply(fresh);
    for (std::size_t i=0; i<5; ++i) require(book.top().bids[i] == deeper[i], "reference recovered");
    std::cout << "PASS independent deeper-book fixture\n";
}
void csv_and_pipeline() {
    OrderBook small; small.apply(anchor());
    const std::string expected = "1700000000,123,1,1,P,N,100,99,98,97,96,10,9,8,7,6,102,103,104,105,106,12,13,14,15,16\r\n";
    require(format_book_row({{1700000000,123},1},anchor(),small.top(),1) == expected, "independent exact 26-column row");
    require(std::count(expected.begin(),expected.end(),',') == 25, "26 logical columns");
    for (const auto& pair : std::vector<std::pair<DepthEvent,std::string>>{
        {diff(101,101,{{100,1}}),",D,B,"},{diff(101,101,{},{{102,1}}),",D,S,"},
        {diff(101,101,{{100,1}},{{102,1}}),",D,N,"},{diff(101,101),",D,N,"}}) {
        require(format_book_row({{3,4},8},pair.first,small.top(),9).find(pair.second) != std::string::npos,"side policy");
    }
    rejects([&] { format_book_row({{3,1000000000},1},anchor(),small.top(),1); }, "bad nanos");
    rejects([&] { format_book_row({{3,4},1},anchor(),small.top(),0); }, "zero book sequence");
    const auto root = std::filesystem::temp_directory_path() / ("binance-book-test-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto market = root / "market.csv", output = order_book_path(market);
    require(output.filename() == "market_orderbook.csv", "suffix");
    MarketDataCsv audit(market); OrderBookCsv csv(output); OrderBook book; BookCounters counters{};
    process_event({{1000,1},1},wire(anchor()),audit,book,csv,counters);
    process_event({{999,2},2},wire(diff(101,101,{{100,5}})),audit,book,csv,counters);
    const auto trade = parse_envelope(R"({"stream":"btcusdt@trade","data":{"s":"BTCUSDT","p":"100","q":"3"}})");
    const auto before_trade = book.top();
    require(!process_event({{999,3},3},trade,audit,book,csv,counters), "trade produces no row");
    require(book.top() == before_trade, "trade does not subtract size");
    process_event({{999,4},4},wire(diff(101,101,{{100,5}})),audit,book,csv,counters);
    process_event({{999,5},1,1},trade,audit,book,csv,counters);
    require(!book.ready(), "trade can signal new epoch");
    process_event({{999,6},2,1},wire(anchor(1)),audit,book,csv,counters);
    require(audit.rows() == 6 && csv.rows() == 3, "audit/book counts");
    require(counters[0] == 2 && counters[1] == 1 && counters[3] == 1, "applied and skipped counters");
    audit.close();
    const auto before = book.top();
    rejects([&] { process_event({{999,7},3,1},wire(diff(2,2,{{100,42}})),audit,book,csv,counters); }, "audit write failure");
    require(book.top() == before && book.revision() == 1 && csv.rows() == 3, "audit failure prevented mutation");
    csv.flush(); csv.close(); csv.close();
    const auto content = read(output);
    require(content.rfind(std::string(book_header)+"\r\n",0) == 0, "book header");
    require(content.find("999,2,2,1,D,B,10000000000,9900000000") != std::string::npos, "original nonmonotonic receive time");
    require(content.find("999,6,3,1,P,N,") != std::string::npos, "seqNo survives epoch");
    require(std::count(content.begin(),content.end(),'\n') == 4, "file row count");
    std::istringstream lines(content); std::string line;
    while (std::getline(lines,line)) require(std::count(line.begin(),line.end(),',') == 25,"every book row 26 columns");
    const auto audit_content = read(market);
    require(audit_content.rfind(std::string(market_header)+"\r\n",0) == 0, "unchanged nine-column header");
    require(audit_content.find("999,5,spot,trade,0,1,1,BTCUSDT,") != std::string::npos, "audit epoch metadata");
    rejects([&] { OrderBookCsv duplicate(output); }, "book overwrite forbidden");
    rejects([&] { csv.write({{999,7},3},anchor(),book.top()); }, "closed book output");
    require(csv.rows() == 3, "failed write did not advance seqNo");
    std::filesystem::remove(market); std::filesystem::remove(output); std::filesystem::remove(root);
    std::cout << "PASS exact CSV rows, sides, timestamps, epochs, trades and audit-before-mutation\n";
}
}
int main() {
    try {
        operations(); sequencing(); invalid_and_short(); decoding(); deeper_reference(); csv_and_pipeline();
        std::cout << "Passed " << checks << " order-book/decoder/pipeline checks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
