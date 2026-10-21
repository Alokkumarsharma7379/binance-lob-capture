#pragma once
#include "event_processing.hpp"
#include <filesystem>
#include <istream>

namespace binance_capture {
struct ReplayStats {
    std::uint64_t market_rows = 0;
    BookCounters decisions{};
};
ReplayStats replay_market_csv(std::istream& input, OrderBookCsv& output);
int run_replay(const std::filesystem::path& input_path, const std::filesystem::path& output_directory);
}
