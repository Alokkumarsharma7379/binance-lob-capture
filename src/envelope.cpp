#include "envelope.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace binance_capture {

MarketEvent parse_envelope(const std::string& message, const std::string& symbol) {
    const auto envelope = nlohmann::ordered_json::parse(message, nullptr, false);
    if (!envelope.is_object() || !envelope.contains("stream")
        || !envelope["stream"].is_string() || !envelope.contains("data")
        || !envelope["data"].is_object()) {
        throw std::invalid_argument("expected a JSON object with string stream and object data");
    }

    const auto& name = envelope["stream"].get_ref<const std::string&>();
    StreamKind kind;
    if (name == "btcusdt@depth@100ms") kind = StreamKind::depth_diff;
    else if (name == "btcusdt@depth5@100ms") kind = StreamKind::depth5;
    else if (name == "btcusdt@trade") kind = StreamKind::trade;
    else if (name == "!serverShutdown") kind = StreamKind::server_shutdown;
    else throw std::invalid_argument("unexpected stream: " + name);

    const auto& data = envelope["data"];
    if (symbol != "BTCUSDT") throw std::invalid_argument("unsupported configured symbol: " + symbol);
    if (data.contains("s") && (!data["s"].is_string() || data["s"] != symbol)) {
        throw std::invalid_argument("payload symbol disagrees with configured stream");
    }
    return {kind, symbol, data.dump()};
}

const char* stream_kind_name(StreamKind kind) {
    switch (kind) {
    case StreamKind::depth_diff: return "depth_diff";
    case StreamKind::depth5: return "depth5";
    case StreamKind::trade: return "trade";
    case StreamKind::server_shutdown: break;
    }
    throw std::invalid_argument("control notice is not a market-data row");
}

const char* stream_name(StreamKind kind) {
    switch (kind) {
    case StreamKind::depth_diff: return "btcusdt@depth@100ms";
    case StreamKind::depth5: return "btcusdt@depth5@100ms";
    case StreamKind::trade: return "btcusdt@trade";
    case StreamKind::server_shutdown: return "!serverShutdown";
    }
    throw std::invalid_argument("unknown stream kind");
}

} // namespace binance_capture
