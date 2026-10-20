#include "envelope.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    using binance_capture::StreamKind;
    using binance_capture::parse_envelope;
    try {
        const struct {
            const char* name;
            StreamKind kind;
        } streams[] = {
            {"btcusdt@depth@100ms", StreamKind::depth_diff},
            {"btcusdt@depth5@100ms", StreamKind::depth5},
            {"btcusdt@trade", StreamKind::trade},
            {"!serverShutdown", StreamKind::server_shutdown},
        };
        for (const auto& stream : streams) {
            const auto message = std::string("{\"stream\":\"") + stream.name + "\",\"data\":{}}";
            if (parse_envelope(message).kind != stream.kind
                || std::string(binance_capture::stream_name(stream.kind)) != stream.name) {
                throw std::runtime_error("incorrect stream classification");
            }
        }

        const char* invalid[] = {
            "not JSON",
            "[]",
            "null",
            "{}",
            "{\"data\":{}}",
            "{\"stream\":7,\"data\":{}}",
            "{\"stream\":\"btcusdt@trade\"}",
            "{\"stream\":\"btcusdt@trade\",\"data\":null}",
            "{\"stream\":\"btcusdt@trade\",\"data\":[]}",
            "{\"stream\":\"ethusdt@trade\",\"data\":{}}",
            "{\"stream\":\"btcusdt@aggTrade\",\"data\":{}}",
        };
        for (const auto* message : invalid) {
            bool rejected = false;
            try {
                parse_envelope(message);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            if (!rejected) throw std::runtime_error("invalid envelope was accepted");
        }
        std::cout << "Passed 4 stream classifications and 11 invalid-envelope cases.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Envelope test failed: " << error.what() << '\n';
        return 1;
    }
}
