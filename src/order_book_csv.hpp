#pragma once
#include "market_data.hpp"
#include "order_book.hpp"

namespace binance_capture {
inline constexpr const char* book_header =
    "tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,"
    "bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,ask0,ask1,ask2,ask3,ask4,"
    "ask_size0,ask_size1,ask_size2,ask_size3,ask_size4";
std::string format_book_row(const EventMetadata& metadata, const DepthEvent& event,
                            const TopFive& top, std::uint64_t sequence);
std::filesystem::path order_book_path(const std::filesystem::path& market_path);

class OrderBookCsv {
public:
    explicit OrderBookCsv(const std::filesystem::path& path) : file_(path, book_header) {}
    void write(const EventMetadata& metadata, const DepthEvent& event, const TopFive& top);
    void flush() { file_.flush(); }
    void close() { file_.close(); }
    std::uint64_t rows() const { return rows_; }
private:
    CsvFile file_;
    std::uint64_t rows_ = 0;
};
}
