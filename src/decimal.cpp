#include "decimal.hpp"

#include <limits>
#include <stdexcept>

namespace binance_capture {

std::int64_t parse_decimal(std::string_view text) {
    if (text.empty()) throw std::invalid_argument("empty decimal");
    std::int64_t value = 0;
    std::size_t fractional_digits = 0;
    bool fraction = false, integer_digit = false, fractional_digit = false;
    for (char character : text) {
        if (character == '.' && !fraction && integer_digit) {
            fraction = true;
            continue;
        }
        if (character < '0' || character > '9') throw std::invalid_argument("invalid decimal syntax");
        const auto digit = character - '0';
        if (fraction) {
            fractional_digit = true;
            if (fractional_digits == 8) {
                if (digit != 0) throw std::invalid_argument("nonzero decimal precision beyond 8 places");
                continue;
            }
            ++fractional_digits;
        } else integer_digit = true;
        if (value > (std::numeric_limits<std::int64_t>::max() - digit) / 10) {
            throw std::overflow_error("scaled decimal exceeds int64");
        }
        value = value * 10 + digit;
    }
    if (!integer_digit || (fraction && !fractional_digit)) throw std::invalid_argument("invalid decimal syntax");
    for (; fractional_digits < 8; ++fractional_digits) {
        if (value > std::numeric_limits<std::int64_t>::max() / 10) {
            throw std::overflow_error("scaled decimal exceeds int64");
        }
        value *= 10;
    }
    return value;
}

} // namespace binance_capture
