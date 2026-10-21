#include "session.hpp"
#include "config.hpp"
#include "envelope.hpp"
#include "market_data.hpp"
#include "event_processing.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <array>
#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>

namespace binance_capture {
namespace {

namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;
using ErrorCode = boost::system::error_code;
using SteadyClock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr const char* host = "stream.binance.com";
constexpr const char* port = "9443";
constexpr const char* target =
    "/stream?streams=btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade";

void validate_timer_duration(std::int64_t duration, SteadyClock::time_point now) {
    const auto remaining = SteadyClock::time_point::max() - now;
    const auto maximum = std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
    if (duration > maximum) {
        throw std::invalid_argument("--duration exceeds the steady-clock timer range");
    }
}

class Session {
public:
    Session(std::int64_t duration_seconds, MarketDataCsv& csv, OrderBookCsv& book_csv)
        : duration_seconds_(duration_seconds), csv_(csv), book_csv_(book_csv) {
        tls_.set_default_verify_paths();
        tls_.set_options(ssl::context::no_sslv2 | ssl::context::no_sslv3
                         | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
        ws_.next_layer().set_verify_mode(ssl::verify_peer);
        ws_.next_layer().set_verify_callback(ssl::host_name_verification(host));
        if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host)) {
            const ErrorCode error(static_cast<int>(ERR_get_error()), net::error::get_ssl_category());
            throw boost::system::system_error(error, "setting TLS SNI");
        }
        ws_.read_message_max(1024 * 1024);
        ws_.control_callback([this](websocket::frame_type kind, beast::string_view) {
            if (kind == websocket::frame_type::ping) ++pings_;
        });
#ifdef SIGBREAK
        signals_.add(SIGBREAK);
#endif
    }

    int run() {
        started_ = SteadyClock::now();
        if (duration_seconds_ > 0) {
            validate_timer_duration(duration_seconds_, started_);
            duration_.expires_after(std::chrono::seconds(duration_seconds_));
            duration_.async_wait([this](ErrorCode error) {
                if (!error) request_stop("duration expired");
            });
        }
        signals_.async_wait([this](ErrorCode error, int number) {
            if (!error) request_stop("signal " + std::to_string(number));
        });

        std::cerr << "Connecting to wss://" << host << ':' << port << target << '\n';
        set_deadline("DNS resolution");
        resolver_.async_resolve(host, port,
            [this](ErrorCode error, tcp::resolver::results_type results) {
                if (stopping_) return;
                if (error) return connection_error("DNS resolution", error);
                dns_ok_ = true;
                std::cerr << "DNS resolved.\n";
                set_deadline("TCP connection");
                beast::get_lowest_layer(ws_).async_connect(results,
                    [this](ErrorCode connect_error, const tcp::endpoint&) {
                        on_connect(connect_error);
                    });
            });

        // Session is stack-owned and remains alive until all cancelled handlers drain.
        io_.run();
        try {
            csv_.close();
        } catch (const std::exception& error) {
            failed_ = true;
            ++io_errors_;
            std::cerr << "CSV finalization error: " << error.what() << '\n';
        }
        try {
            book_csv_.close();
        } catch (const std::exception& error) {
            failed_ = true;
            ++io_errors_;
            std::cerr << "Book CSV finalization error: " << error.what() << '\n';
        }
        const bool all_streams = counts_[0] > 0 && counts_[1] > 0 && counts_[2] > 0;
        const bool success = requested_stop_ && websocket_ok_ && all_streams && book_csv_.rows() > 0 && !failed_;
        print_summary();
        if (!all_streams) std::cerr << "Capture incomplete: not all three required streams were observed.\n";
        return success ? 0 : 1;
    }

private:
    void set_deadline(const char* phase) {
        const auto generation = ++deadline_generation_;
        deadline_.expires_after(15s);
        deadline_.async_wait([this, phase, generation](ErrorCode error) {
            if (!error && !stopping_ && generation == deadline_generation_) {
                connection_error(phase, net::error::timed_out);
            }
        });
    }

    void on_connect(ErrorCode error) {
        if (stopping_) return;
        if (error) return connection_error("TCP connection", error);
        tcp_ok_ = true;
        std::cerr << "TCP connected.\n";
        set_deadline("TLS handshake");
        ws_.next_layer().async_handshake(ssl::stream_base::client,
            [this](ErrorCode handshake_error) {
                if (stopping_) return;
                if (handshake_error) return connection_error("TLS handshake", handshake_error);
                tls_ok_ = true;
                std::cerr << "TLS handshake and certificate/hostname verification succeeded.\n";
                beast::get_lowest_layer(ws_).expires_never();
                websocket::stream_base::timeout timeouts{15s, 60s, false};
                ws_.set_option(timeouts);
                set_deadline("WebSocket handshake");
                ws_.async_handshake(std::string(host) + ':' + port, target,
                    [this](ErrorCode websocket_error) { on_handshake(websocket_error); });
            });
    }

    void on_handshake(ErrorCode error) {
        if (stopping_) return;
        if (error) return connection_error("WebSocket handshake", error);
        websocket_ok_ = true;
        ++deadline_generation_;
        deadline_.cancel();
        std::cerr << "WebSocket connected; receiving the three combined streams.\n";
        report_later();
        flush_later();
        read();
    }

    void read() {
        ws_.async_read(buffer_, [this](ErrorCode error, std::size_t) {
            const auto received = std::chrono::system_clock::now();
            if (stopping_) return;
            if (error) return connection_error("WebSocket read/disconnection", error);
            last_receive_ = receive_timestamp(received);
            ++messages_;
            if (!ws_.got_text()) return envelope_error("received binary data on the JSON stream");
            next_connection_sequence(conn_seq_);

            const std::string message = beast::buffers_to_string(buffer_.data());
            buffer_.consume(buffer_.size());
            MarketEvent event;
            try {
                event = parse_envelope(message);
            } catch (const std::invalid_argument& parse_error) {
                return envelope_error(parse_error.what());
            }
            if (event.kind == StreamKind::server_shutdown) {
                return connection_error("Binance server shutdown notice", net::error::connection_aborted);
            }
            try {
                const auto decision = process_event({last_receive_, conn_seq_}, event,
                                                     csv_, book_, book_csv_, book_counts_);
                if (decision && (*decision == BookDecision::gap || *decision == BookDecision::coverage_lost
                                 || *decision == BookDecision::crossed)) {
                    std::cerr << "Book recovery: " << decision_name(*decision)
                              << " conn_seq=" << conn_seq_ << '\n';
                }
            } catch (const std::ios_base::failure& write_error) {
                return output_error(write_error.what());
            } catch (const std::exception& book_error) {
                book_.invalidate();
                failed_ = true;
                ++book_errors_;
                stop_reason_ = "invalid or inconsistent book event";
                std::cerr << "Book error at conn_seq=" << conn_seq_ << ": " << book_error.what() << '\n';
                return finish();
            }
            const auto kind = event.kind;
            auto& count = counts_[static_cast<std::size_t>(kind)];
            ++count;
            if (count == 1) {
                std::cout << "Sample stream=" << stream_name(kind)
                          << " recv_tsec=" << last_receive_.seconds
                          << " recv_tnsec=" << last_receive_.nanoseconds << '\n'
                          << message << '\n' << std::flush;
            }
            read();
        });
    }

    void report_later() {
        report_.expires_after(5s);
        report_.async_wait([this](ErrorCode error) {
            if (error || stopping_) return;
            print_counts();
            report_later();
        });
    }

    void flush_later() {
        flush_.expires_after(1s);
        flush_.async_wait([this](ErrorCode error) {
            if (error || stopping_) return;
            try {
                csv_.flush();
                book_csv_.flush();
            } catch (const std::exception& flush_error) {
                return output_error(flush_error.what());
            }
            flush_later();
        });
    }

    void request_stop(const std::string& reason) {
        if (stopping_) return;
        stopping_ = true;
        requested_stop_ = true;
        stop_reason_ = reason;
        duration_.cancel();
        deadline_.cancel();
        report_.cancel();
        flush_.cancel();
        resolver_.cancel();
        signals_.cancel();
        std::cerr << "Stopping: " << reason << '\n';
        if (!websocket_ok_) return finish();

        shutdown_.expires_after(5s);
        shutdown_.async_wait([this](ErrorCode error) {
            if (!error && !finished_) {
                failed_ = true;
                ++connection_errors_;
                std::cerr << "Shutdown deadline reached; closing the socket.\n";
                finish();
            }
        });
        // Beast coordinates close with the outstanding read and tears down TLS.
        ws_.async_close(websocket::close_code::normal, [this](ErrorCode error) {
            if (finished_) return;
            // Only tolerate missing close_notify after a normal peer WebSocket close.
            // A truncated live read still goes through connection_error and fails.
            if (error == ssl::error::stream_truncated
                && ws_.reason().code == websocket::close_code::normal) {
                ++tls_shutdown_warnings_;
                std::cerr << "TLS shutdown warning: peer sent a normal WebSocket close (1000) "
                             "but omitted TLS close_notify.\n";
            } else if (error) {
                failed_ = true;
                ++connection_errors_;
                std::cerr << "WebSocket/TLS close failed: " << error.message() << '\n';
            }
            finish();
        });
    }

    void connection_error(const char* phase, ErrorCode error) {
        if (stopping_) return;
        failed_ = true;
        ++connection_errors_;
        stop_reason_ = std::string(phase) + " failed";
        std::cerr << phase << " failed: " << error.message() << '\n';
        finish();
    }

    void envelope_error(const std::string& reason) {
        failed_ = true;
        ++parse_errors_;
        stop_reason_ = "invalid JSON envelope";
        std::cerr << "Envelope error: " << reason << '\n';
        finish();
    }

    void output_error(const std::string& reason) {
        failed_ = true;
        ++io_errors_;
        stop_reason_ = "CSV output failed";
        std::cerr << "CSV error: " << reason << '\n';
        finish();
    }

    void finish() {
        if (finished_) return;
        book_.invalidate();
        finished_ = true;
        stopping_ = true;
        resolver_.cancel();
        deadline_.cancel();
        duration_.cancel();
        shutdown_.cancel();
        report_.cancel();
        flush_.cancel();
        signals_.cancel();
        ErrorCode ignored;
        auto& socket = beast::get_lowest_layer(ws_).socket();
        socket.shutdown(tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
    }

    void print_counts() const {
        std::cerr << "Messages=" << messages_ << " depth_diff=" << counts_[0]
                  << " depth5=" << counts_[1] << " trade=" << counts_[2]
                  << " market_rows=" << csv_.rows() << '\n';
    }

    void print_summary() const {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            SteadyClock::now() - started_).count();
        std::cerr << "Summary: DNS=" << dns_ok_ << " TCP=" << tcp_ok_
                  << " TLS=" << tls_ok_ << " WebSocket=" << websocket_ok_ << '\n';
        print_counts();
        std::cerr << "Book rows=" << book_csv_.rows();
        for (std::size_t i = 0; i < book_counts_.size(); ++i) {
            std::cerr << ' ' << decision_name(static_cast<BookDecision>(i)) << '=' << book_counts_[i];
        }
        std::cerr << " consistency_checks=" << book_.consistency_checks()
                  << " book_errors=" << book_errors_ << '\n';
        std::cerr << "Parse errors=" << parse_errors_ << " connection errors=" << connection_errors_
                  << " I/O errors=" << io_errors_
                  << " TLS shutdown warnings=" << tls_shutdown_warnings_
                  << " ping frames=" << pings_ << " elapsed_ms=" << elapsed
                  << " stop_reason=" << stop_reason_ << '\n';
    }

    net::io_context io_;
    ssl::context tls_{ssl::context::tls_client};
    tcp::resolver resolver_{io_};
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_{io_, tls_};
    beast::flat_buffer buffer_;
    net::steady_timer deadline_{io_}, duration_{io_}, shutdown_{io_}, report_{io_};
    net::steady_timer flush_{io_};
    net::signal_set signals_{io_, SIGINT, SIGTERM};
    std::int64_t duration_seconds_;
    MarketDataCsv& csv_;
    OrderBookCsv& book_csv_;
    OrderBook book_;
    BookCounters book_counts_{};
    std::uint64_t book_errors_ = 0;
    SteadyClock::time_point started_{};
    ReceiveTimestamp last_receive_{};
    std::array<std::uint64_t, 3> counts_{};
    std::uint64_t messages_ = 0, parse_errors_ = 0, connection_errors_ = 0, pings_ = 0;
    std::uint64_t tls_shutdown_warnings_ = 0;
    std::uint64_t conn_seq_ = 0, io_errors_ = 0;
    std::size_t deadline_generation_ = 0;
    bool dns_ok_ = false, tcp_ok_ = false, tls_ok_ = false, websocket_ok_ = false;
    bool stopping_ = false, finished_ = false, requested_stop_ = false, failed_ = false;
    std::string stop_reason_ = "event loop ended unexpectedly";
};

} // namespace

int run_capture(const Config& config) {
    validate_timer_duration(config.duration_seconds, SteadyClock::now());
    const auto path = market_data_path(config.output_dir, std::chrono::system_clock::now());
    const auto book_path = order_book_path(path);
    if (std::filesystem::exists(path) || std::filesystem::exists(book_path)) {
        throw std::runtime_error("output CSV already exists; choose a fresh --output-dir");
    }
    MarketDataCsv csv(path);
    OrderBookCsv book_csv(book_path);
    std::cerr << "Market CSV: " << path.u8string() << '\n';
    std::cerr << "Order-book CSV: " << book_path.u8string() << '\n';
    Session session(config.duration_seconds, csv, book_csv);
    return session.run();
}

} // namespace binance_capture
