#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace binance_capture {

enum class StreamKind { depth_diff, depth5, trade, server_shutdown };

struct MarketEvent {
    StreamKind kind;
    std::string symbol;
    std::string payload_json;
    nlohmann::ordered_json data = nlohmann::ordered_json::object();
};

MarketEvent parse_envelope(const std::string& message, const std::string& symbol = "BTCUSDT");
MarketEvent parse_market_payload(StreamKind kind, const std::string& symbol, const std::string& payload);
const char* stream_name(StreamKind kind);
const char* stream_kind_name(StreamKind kind);

} // namespace binance_capture
