#include "order_book_csv.hpp"
#include <limits>
#include <stdexcept>

namespace binance_capture {
std::filesystem::path order_book_path(const std::filesystem::path& market_path) {
    return market_path.parent_path() / (market_path.stem().u8string() + "_orderbook.csv");
}
std::string format_book_row(const EventMetadata& metadata, const DepthEvent& event,
                            const TopFive& top, std::uint64_t sequence) {
    if (sequence == 0 || metadata.received.nanoseconds < 0 || metadata.received.nanoseconds >= 1000000000) {
        throw std::invalid_argument("invalid book row metadata");
    }
    const bool snapshot = event.kind == DepthKind::snapshot;
    const char side = snapshot || event.bids.empty() == event.asks.empty() ? 'N'
        : (event.bids.empty() ? 'S' : 'B');
    std::string row = std::to_string(metadata.received.seconds) + ','
        + std::to_string(metadata.received.nanoseconds) + ',' + std::to_string(sequence)
        + ",1," + (snapshot ? "P" : "D") + ',' + side;
    for (const auto& level : top.bids) row += ',' + std::to_string(level.price);
    for (const auto& level : top.bids) row += ',' + std::to_string(level.quantity);
    for (const auto& level : top.asks) row += ',' + std::to_string(level.price);
    for (const auto& level : top.asks) row += ',' + std::to_string(level.quantity);
    return row + "\r\n";
}
void OrderBookCsv::write(const EventMetadata& metadata, const DepthEvent& event, const TopFive& top) {
    if (rows_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("book sequence exhausted");
    file_.write(format_book_row(metadata, event, top, rows_ + 1));
    ++rows_;
}
}
