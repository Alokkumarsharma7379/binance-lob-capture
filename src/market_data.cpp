#include "market_data.hpp"

#include <ctime>
#include <limits>
#include <stdexcept>

namespace binance_capture {

ReceiveTimestamp receive_timestamp(std::chrono::system_clock::time_point now) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
    auto remainder = (elapsed - seconds).count();
    if (remainder < 0) {
        seconds -= std::chrono::seconds(1);
        remainder += 1000000000;
    }
    return {seconds.count(), static_cast<std::int32_t>(remainder)};
}

std::uint64_t next_connection_sequence(std::uint64_t& sequence) {
    if (sequence == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("connection sequence exhausted");
    }
    return ++sequence;
}

std::string csv_escape(const std::string& field) {
    if (field.find_first_of(",\"\r\n") == std::string::npos) return field;
    std::string result;
    result.reserve(field.size() + 2);
    result += '"';
    for (const char character : field) {
        if (character == '"') result += '"';
        result += character;
    }
    result += '"';
    return result;
}

std::string format_market_row(const EventMetadata& metadata, const MarketEvent& event) {
    if (metadata.received.nanoseconds < 0 || metadata.received.nanoseconds >= 1000000000
        || metadata.conn_seq == 0) {
        throw std::invalid_argument("invalid market-data metadata");
    }
    return std::to_string(metadata.received.seconds) + ','
        + std::to_string(metadata.received.nanoseconds) + ",spot,"
        + stream_kind_name(event.kind) + ",0," + std::to_string(metadata.conn_epoch)
        + "," + std::to_string(metadata.conn_seq) + ','
        + csv_escape(event.symbol) + ',' + csv_escape(event.payload_json) + "\r\n";
}

std::filesystem::path market_data_path(const std::filesystem::path& directory,
                                      std::chrono::system_clock::time_point started) {
    const auto time = std::chrono::system_clock::to_time_t(started);
    const auto* utc = std::gmtime(&time); // Called on the single event-loop thread.
    char date[11]{};
    if (!utc || std::strftime(date, sizeof(date), "%Y-%m-%d", utc) == 0) {
        throw std::runtime_error("cannot format UTC run-start date");
    }
    return directory / (std::string("market_data_spot_BTCUSDT_") + date + ".csv");
}

MarketDataCsv::MarketDataCsv(const std::filesystem::path& path) : file_(path, market_header) {}

void MarketDataCsv::write(const EventMetadata& metadata, const MarketEvent& event) {
    if (rows_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("CSV row counter exhausted");
    }
    file_.write(format_market_row(metadata, event));
    ++rows_;
}

} // namespace binance_capture
