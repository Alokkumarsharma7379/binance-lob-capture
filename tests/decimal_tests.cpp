#include "decimal.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    using binance_capture::parse_decimal;
    try {
        const struct { const char* text; std::int64_t expected; } valid[] = {
            {"0", 0}, {"0.00000000", 0}, {"1", 100000000}, {"1.0", 100000000},
            {"001.0000000000", 100000000}, {"101.25", 10125000000}, {"0.003", 300000},
            {"0.00000001", 1}, {"92233720368.54775807", std::numeric_limits<std::int64_t>::max()},
            {"92233720368.54775807000", std::numeric_limits<std::int64_t>::max()}
        };
        for (const auto& test : valid) {
            if (parse_decimal(test.text) != test.expected) throw std::runtime_error("decimal value mismatch");
        }
        for (const char* text : {"", ".1", "1.", "-1", "+1", "1e2", " 1", "1 ", "1..2", "NaN",
                                "0.000000001", "1.1234567890", "92233720368.54775808",
                                "92233720369", "999999999999999999999999"}) {
            bool rejected = false;
            try { parse_decimal(text); } catch (const std::exception&) { rejected = true; }
            if (!rejected) throw std::runtime_error("bad decimal accepted");
        }
        std::cout << "Passed 10 exact decimal cases and 15 syntax/precision/overflow rejections.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
