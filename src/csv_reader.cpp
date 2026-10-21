#include "csv_reader.hpp"
#include <stdexcept>
#include <utility>

namespace binance_capture {
bool read_csv_record(std::istream& input, std::vector<std::string>& fields) {
    enum class State { start, plain, quoted, after_quote };
    State state = State::start;
    fields.clear();
    std::string field;
    std::size_t bytes = 0;
    const auto finish_field = [&] {
        if (fields.size() == 9) throw std::invalid_argument("CSV record has more than nine fields");
        fields.push_back(std::move(field));
        field.clear();
    };
    for (;;) {
        const int next = input.get();
        if (next == std::char_traits<char>::eof()) {
            if (input.bad() || !input.eof()) throw std::runtime_error("CSV input read failed");
            if (state == State::quoted) throw std::invalid_argument("unterminated quoted CSV field");
            if (bytes == 0) return false;
            finish_field();
            return true;
        }
        if (++bytes > max_csv_record_bytes) throw std::invalid_argument("CSV record exceeds byte limit");
        const char character = static_cast<char>(next);
        if (character == '\0') throw std::invalid_argument("NUL byte in CSV input");
        if (state == State::quoted) {
            if (character == '"') state = State::after_quote;
            else field += character;
            continue;
        }
        if (state == State::after_quote && character == '"') {
            field += '"';
            state = State::quoted;
            continue;
        }
        if (character == ',' || character == '\n' || character == '\r') {
            finish_field();
            state = State::start;
            if (character == ',') continue;
            if (character == '\r') {
                if (input.get() != '\n') throw std::invalid_argument("bare CR outside quoted CSV field");
                if (++bytes > max_csv_record_bytes) throw std::invalid_argument("CSV record exceeds byte limit");
            }
            return true;
        }
        if (state == State::after_quote) throw std::invalid_argument("unexpected byte after CSV closing quote");
        if (character == '"') {
            if (state != State::start) throw std::invalid_argument("quote in unquoted CSV field");
            state = State::quoted;
        } else {
            state = State::plain;
            field += character;
        }
    }
}
}
