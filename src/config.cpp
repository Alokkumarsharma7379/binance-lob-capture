#include "config.hpp"

#include <limits>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>

namespace binance_capture {
namespace {

std::int64_t parse_duration(const std::string& value) {
    std::int64_t seconds = 0;
    const auto maximum = std::numeric_limits<std::int64_t>::max();

    for (const char character : value) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument("--duration must be a positive integer number of seconds");
        }

        const int digit = character - '0';
        if (seconds > (maximum - digit) / 10) {
            throw std::invalid_argument("--duration exceeds the supported integer range");
        }
        seconds = seconds * 10 + digit;
    }

    if (seconds == 0) {
        throw std::invalid_argument("--duration must be greater than zero");
    }
    return seconds;
}

} // namespace

Config parse_arguments(int argc, char* argv[]) {
    Config config;
    std::set<std::string> seen_options;

    for (int index = 1; index < argc; ++index) {
        std::string option = argv[index];
        if (option == "-h") {
            option = "--help";
        }

        if (option != "--help" && option != "--venue" && option != "--symbols"
            && option != "--output-dir" && option != "--duration") {
            throw std::invalid_argument("unknown argument: " + option);
        }
        if (!seen_options.insert(option).second) {
            throw std::invalid_argument("duplicate option: " + option);
        }
        if (option == "--help") {
            config.show_help = true;
            continue;
        }

        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + option);
        }
        const std::string value = argv[++index];
        if (value.empty() || value.compare(0, 2, "--") == 0 || value == "-h") {
            throw std::invalid_argument("missing value for " + option);
        }

        if (option == "--venue") {
            if (value != "spot") {
                throw std::invalid_argument("--venue supports only spot in the initial scope");
            }
            config.venue = value;
        } else if (option == "--symbols") {
            if (value != "BTCUSDT") {
                throw std::invalid_argument("--symbols supports only BTCUSDT in the initial scope");
            }
            config.symbol = value;
        } else if (option == "--output-dir") {
            config.output_dir = value;
        } else if (option == "--duration") {
            config.duration_seconds = parse_duration(value);
        }
    }

    return config;
}

void print_help(std::ostream& output) {
    output
        << "Usage: binance_capture [options]\n"
        << "\n"
        << "Stage 4C: capture Binance Spot BTCUSDT messages to a nine-column market CSV.\n"
        << "\n"
        << "Options:\n"
        << "  --help, -h           Show this help\n"
        << "  --venue spot         Venue (default: spot; only spot is accepted)\n"
        << "  --symbols BTCUSDT    Instrument (default: BTCUSDT; only BTCUSDT is accepted)\n"
        << "  --output-dir PATH    Capture directory (default: ./output; existing files refused)\n"
        << "  --duration SECONDS   Positive integer within timer range; omitted means until stopped\n"
        << "\n"
        << "Use separate option and value arguments; duplicate options are errors.\n"
        << "Exit codes: 0 = help or requested stop after receiving all three streams,\n"
        << "            1 = failed/incomplete capture, 2 = invalid arguments.\n";
}

} // namespace binance_capture
