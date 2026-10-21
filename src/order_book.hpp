#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace binance_capture {

struct Level {
    std::int64_t price = 0;
    std::int64_t quantity = 0;
};
bool operator==(const Level& left, const Level& right);
struct TopFive {
    std::array<Level, 5> bids{};
    std::array<Level, 5> asks{};
};
bool operator==(const TopFive& left, const TopFive& right);

enum class DepthKind { diff, snapshot };
struct DepthEvent {
    DepthKind kind;
    std::uint64_t first;
    std::uint64_t last;
    std::vector<Level> bids;
    std::vector<Level> asks;
};
enum class BookDecision {
    applied_snapshot, applied_diff, stale_snapshot, stale_diff,
    waiting, gap, coverage_lost, crossed, count
};
const char* decision_name(BookDecision decision);
bool applied(BookDecision decision);

class OrderBook {
public:
    BookDecision apply(const DepthEvent& event, std::uint64_t epoch = 0);
    void observe_epoch(std::uint64_t epoch);
    void invalidate();
    TopFive top() const;
    bool ready() const { return ready_; }
    std::uint64_t revision() const { return revision_; }
    std::uint64_t consistency_checks() const { return consistency_checks_; }

private:
    std::map<std::int64_t, std::int64_t, std::greater<std::int64_t>> bids_;
    std::map<std::int64_t, std::int64_t> asks_;
    std::optional<std::uint64_t> epoch_;
    std::uint64_t revision_ = 0, highest_seen_ = 0;
    std::uint64_t consistency_checks_ = 0;
    std::int64_t bid_floor_ = 0, ask_ceiling_ = 0;
    bool ready_ = false;
    bool has_state_ = false;
};

} // namespace binance_capture
