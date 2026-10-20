#include "market_data.hpp"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

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
        + stream_kind_name(event.kind) + ",0,0," + std::to_string(metadata.conn_seq) + ','
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

void write_csv_bytes(std::FILE* file, const std::string& bytes) {
    if (std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size() || std::ferror(file)) {
        throw std::runtime_error("CSV write failed");
    }
}

void flush_csv_file(std::FILE* file) {
    if (std::fflush(file) != 0 || std::ferror(file)) {
        throw std::runtime_error("CSV flush failed");
    }
}

MarketDataCsv::MarketDataCsv(const std::filesystem::path& path) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    // C++17 streams lack exclusive creation. Only this boundary uses platform file APIs.
#ifdef _WIN32
    const int descriptor = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                                 _S_IREAD | _S_IWRITE);
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(), "creating CSV " + path.u8string());
    }
#ifdef _WIN32
    file_ = _fdopen(descriptor, "wb");
#else
    file_ = ::fdopen(descriptor, "wb");
#endif
    if (!file_) {
        const int saved_error = errno;
#ifdef _WIN32
        _close(descriptor);
#else
        ::close(descriptor);
#endif
        throw std::system_error(saved_error, std::generic_category(), "opening CSV stream");
    }
    try {
        write_csv_bytes(file_, std::string(market_header) + "\r\n");
        flush();
    } catch (...) {
        std::fclose(file_);
        file_ = nullptr;
        throw;
    }
}

MarketDataCsv::~MarketDataCsv() {
    if (file_) std::fclose(file_); // Normal execution uses checked close().
}

void MarketDataCsv::write(const EventMetadata& metadata, const MarketEvent& event) {
    if (!file_) throw std::runtime_error("cannot write a closed CSV");
    if (rows_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("CSV row counter exhausted");
    }
    write_csv_bytes(file_, format_market_row(metadata, event));
    ++rows_;
}

void MarketDataCsv::flush() {
    if (!file_) throw std::runtime_error("cannot flush a closed CSV");
    flush_csv_file(file_);
}

void MarketDataCsv::close() {
    if (!file_) return;
    auto* file = file_;
    file_ = nullptr;
    const bool flush_failed = std::fflush(file) != 0 || std::ferror(file);
    const bool close_failed = std::fclose(file) != 0;
    if (flush_failed || close_failed) throw std::runtime_error("CSV flush/close failed");
}

} // namespace binance_capture
