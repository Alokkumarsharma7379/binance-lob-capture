#include "depth_event.hpp"
#include "decimal.hpp"
#include <stdexcept>

namespace binance_capture {
namespace {
std::uint64_t update_id(const nlohmann::ordered_json& data, const char* key) {
    if (!data.contains(key) || !data[key].is_number_integer()
        || (!data[key].is_number_unsigned() && data[key].get<std::int64_t>() < 0)) {
        throw std::invalid_argument(std::string("invalid depth integer: ") + key);
    }
    return data[key].get<std::uint64_t>();
}
std::vector<Level> levels(const nlohmann::ordered_json& data, const char* key) {
    if (!data.contains(key) || !data[key].is_array() || data[key].size() > 10000) {
        throw std::invalid_argument(std::string("invalid depth array: ") + key);
    }
    std::vector<Level> result;
    result.reserve(data[key].size());
    for (const auto& pair : data[key]) {
        if (!pair.is_array() || pair.size() != 2 || !pair[0].is_string() || !pair[1].is_string()) {
            throw std::invalid_argument("depth level must be two decimal strings");
        }
        result.push_back({parse_decimal(pair[0].get_ref<const std::string&>()),
                          parse_decimal(pair[1].get_ref<const std::string&>())});
    }
    return result;
}
}
DepthEvent decode_depth_event(const MarketEvent& event) {
    const auto& data = event.data;
    if (!data.is_object()) throw std::invalid_argument("missing parsed depth object");
    if (event.kind == StreamKind::depth5) {
        const auto last = update_id(data, "lastUpdateId");
        return {DepthKind::snapshot, last, last, levels(data, "bids"), levels(data, "asks")};
    }
    if (event.kind != StreamKind::depth_diff || !data.contains("e") || data["e"] != "depthUpdate"
        || !data.contains("s") || data["s"] != event.symbol) {
        throw std::invalid_argument("invalid depth event type or symbol");
    }
    (void)update_id(data, "E");
    return {DepthKind::diff, update_id(data, "U"), update_id(data, "u"),
            levels(data, "b"), levels(data, "a")};
}
} // namespace binance_capture
