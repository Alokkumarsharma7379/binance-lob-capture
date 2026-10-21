#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace binance_capture {
void write_csv_bytes(std::FILE* file, const std::string& bytes);
void flush_csv_file(std::FILE* file);

class CsvFile {
public:
    CsvFile(const std::filesystem::path& path, const std::string& header);
    ~CsvFile();
    CsvFile(const CsvFile&) = delete;
    CsvFile& operator=(const CsvFile&) = delete;
    void write(const std::string& bytes);
    void flush();
    void close();
private:
    std::FILE* file_ = nullptr;
};
}
