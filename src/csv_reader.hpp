#pragma once
#include <cstddef>
#include <istream>
#include <string>
#include <vector>

namespace binance_capture {
inline constexpr std::size_t max_csv_record_bytes = 2 * 1024 * 1024 + 4096;
bool read_csv_record(std::istream& input, std::vector<std::string>& fields);
}
