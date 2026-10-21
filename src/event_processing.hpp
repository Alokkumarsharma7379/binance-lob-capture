#pragma once
#include "order_book_csv.hpp"
#include <array>
#include <optional>

namespace binance_capture {
using BookCounters = std::array<std::uint64_t, static_cast<std::size_t>(BookDecision::count)>;
std::optional<BookDecision> process_book_event(const EventMetadata& metadata, const MarketEvent& event,
    OrderBook& book, OrderBookCsv& output, BookCounters& counters);
std::optional<BookDecision> process_event(const EventMetadata& metadata, const MarketEvent& event,
    MarketDataCsv& audit, OrderBook& book, OrderBookCsv& output, BookCounters& counters);
}
