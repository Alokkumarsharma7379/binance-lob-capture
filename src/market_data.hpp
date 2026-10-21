#pragma once

#include "envelope.hpp"
#include "csv_file.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace binance_capture {

struct ReceiveTimestamp {
    std::int64_t seconds;
    std::int32_t nanoseconds;
};

struct EventMetadata {
    ReceiveTimestamp received;
    std::uint64_t conn_seq;
    std::uint64_t conn_epoch = 0;
};

inline constexpr const char* market_header =
    "recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json";

ReceiveTimestamp receive_timestamp(std::chrono::system_clock::time_point now);
std::uint64_t next_connection_sequence(std::uint64_t& sequence);
std::string csv_escape(const std::string& field);
std::string format_market_row(const EventMetadata& metadata, const MarketEvent& event);
std::filesystem::path market_data_path(const std::filesystem::path& directory,
                                      std::chrono::system_clock::time_point started);

class MarketDataCsv {
public:
    explicit MarketDataCsv(const std::filesystem::path& path);
    MarketDataCsv(const MarketDataCsv&) = delete;
    MarketDataCsv& operator=(const MarketDataCsv&) = delete;

    void write(const EventMetadata& metadata, const MarketEvent& event);
    void flush() { file_.flush(); }
    void close() { file_.close(); }
    std::uint64_t rows() const { return rows_; }

private:
    CsvFile file_;
    std::uint64_t rows_ = 0;
};

} // namespace binance_capture
