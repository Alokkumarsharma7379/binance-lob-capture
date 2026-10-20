cmake_minimum_required(VERSION 3.16)
project(binance_lob_capture LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Boost 1.74 CONFIG REQUIRED)
find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS SSL Crypto)
find_package(nlohmann_json 3.10 CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(binance_capture
    src/main.cpp
    src/config.cpp
    src/envelope.cpp
    src/session.cpp
    src/market_data.cpp
)

set_target_properties(binance_capture PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_link_libraries(binance_capture PRIVATE
    Boost::headers
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(WIN32)
    target_link_libraries(binance_capture PRIVATE ws2_32 mswsock)
endif()

if(MSVC)
    target_compile_options(binance_capture PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(binance_capture PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(binance_envelope_tests tests/envelope_tests.cpp src/envelope.cpp)
    target_include_directories(binance_envelope_tests PRIVATE src)
    target_link_libraries(binance_envelope_tests PRIVATE nlohmann_json::nlohmann_json)
    set_target_properties(binance_envelope_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    if(MSVC)
        target_compile_options(binance_envelope_tests PRIVATE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(binance_envelope_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME envelope_validation COMMAND binance_envelope_tests)
    add_executable(binance_market_data_tests tests/market_data_tests.cpp src/market_data.cpp src/envelope.cpp)
    target_include_directories(binance_market_data_tests PRIVATE src)
    target_link_libraries(binance_market_data_tests PRIVATE nlohmann_json::nlohmann_json)
    set_target_properties(binance_market_data_tests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    if(MSVC)
        target_compile_options(binance_market_data_tests PRIVATE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(binance_market_data_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME market_data_csv COMMAND binance_market_data_tests)
    add_test(NAME cli_help COMMAND binance_capture --help)
    set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: binance_capture")
    add_test(NAME cli_rejects_usdm COMMAND binance_capture --venue usdm)
    set_tests_properties(cli_rejects_usdm PROPERTIES WILL_FAIL TRUE)
endif()
