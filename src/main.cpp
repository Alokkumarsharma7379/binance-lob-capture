#include "config.hpp"
#include "session.hpp"
#include "replay.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    try {
        const auto config = binance_capture::parse_arguments(argc, argv);
        if (config.show_help) {
            binance_capture::print_help(std::cout);
            return 0;
        }

        if (!config.replay_file.empty()) {
            return binance_capture::run_replay(config.replay_file, config.output_dir);
        }
        return binance_capture::run_capture(config);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Argument error: " << error.what() << "\nUse --help for usage.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
