#include "event_processing.hpp"
#include "depth_event.hpp"

namespace binance_capture {
std::optional<BookDecision> process_book_event(const EventMetadata& metadata, const MarketEvent& event,
    OrderBook& book, OrderBookCsv& output, BookCounters& counters) {
    try {
        book.observe_epoch(metadata.conn_epoch);
        if (event.kind == StreamKind::trade) return std::nullopt;
        const auto depth = decode_depth_event(event);
        const auto decision = book.apply(depth, metadata.conn_epoch);
        if (applied(decision)) output.write(metadata, depth, book.top());
        ++counters[static_cast<std::size_t>(decision)];
        return decision;
    } catch (...) {
        book.invalidate();
        throw;
    }
}
std::optional<BookDecision> process_event(const EventMetadata& metadata, const MarketEvent& event,
    MarketDataCsv& audit, OrderBook& book, OrderBookCsv& output, BookCounters& counters) {
    audit.write(metadata, event);
    return process_book_event(metadata, event, book, output, counters);
}
}
