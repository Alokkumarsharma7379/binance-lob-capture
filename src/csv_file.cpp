#include "csv_file.hpp"
#include <ios>

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

void write_csv_bytes(std::FILE* file, const std::string& bytes) {
    if (std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size() || std::ferror(file)) {
        throw std::ios_base::failure("CSV write failed");
    }
}

void flush_csv_file(std::FILE* file) {
    if (std::fflush(file) != 0 || std::ferror(file)) {
        throw std::ios_base::failure("CSV flush failed");
    }
}

CsvFile::CsvFile(const std::filesystem::path& path, const std::string& header) {
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
        write_csv_bytes(file_, header + "\r\n");
        flush();
    } catch (...) {
        std::fclose(file_);
        file_ = nullptr;
        throw;
    }
}

CsvFile::~CsvFile() {
    if (file_) std::fclose(file_); // Normal execution uses checked close().
}

void CsvFile::write(const std::string& bytes) {
    if (!file_) throw std::ios_base::failure("cannot write a closed CSV");
    write_csv_bytes(file_, bytes);
}

void CsvFile::flush() {
    if (!file_) throw std::ios_base::failure("cannot flush a closed CSV");
    flush_csv_file(file_);
}

void CsvFile::close() {
    if (!file_) return;
    auto* file = file_;
    file_ = nullptr;
    const bool flush_failed = std::fflush(file) != 0 || std::ferror(file);
    const bool close_failed = std::fclose(file) != 0;
    if (flush_failed || close_failed) throw std::ios_base::failure("CSV flush/close failed");
}

} // namespace binance_capture
