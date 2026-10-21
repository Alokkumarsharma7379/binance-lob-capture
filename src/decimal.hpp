#pragma once

#include <cstdint>
#include <string_view>

namespace binance_capture {

inline constexpr std::int64_t decimal_scale = 100000000;
std::int64_t parse_decimal(std::string_view text);

} // namespace binance_capture
