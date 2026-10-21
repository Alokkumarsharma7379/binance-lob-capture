#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace binance_capture {

struct Config {
    std::string venue = "spot";
    std::string symbol = "BTCUSDT";
    std::string output_dir = "./output";
    std::string replay_file;
    std::int64_t duration_seconds = 0; // Zero means no duration limit.
    bool show_help = false;
};

Config parse_arguments(int argc, char* argv[]);
void print_help(std::ostream& output);

} // namespace binance_capture
