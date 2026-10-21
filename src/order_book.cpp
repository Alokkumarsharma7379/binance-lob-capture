#include "order_book.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace binance_capture {
namespace {
constexpr std::size_t max_changes = 10000;
constexpr std::size_t max_levels = 20000;

void validate_side(const std::vector<Level>& levels, bool snapshot, bool bids) {
    if (levels.size() > (snapshot ? 5 : max_changes)) {
        throw std::invalid_argument("too many depth levels");
    }
    std::set<std::int64_t> prices;
    std::int64_t previous = 0;
    for (const auto& level : levels) {
        if (level.price <= 0 || level.quantity < 0 || (snapshot && level.quantity == 0)) {
            throw std::invalid_argument("invalid depth price or quantity");
        }
        if (!prices.insert(level.price).second) throw std::invalid_argument("duplicate depth price");
        if (snapshot && previous != 0 && (bids ? previous <= level.price : previous >= level.price)) {
            throw std::invalid_argument("snapshot levels are not strictly sorted");
        }
        previous = level.price;
    }
}
}

bool operator==(const Level& a, const Level& b) { return a.price == b.price && a.quantity == b.quantity; }
bool operator==(const TopFive& a, const TopFive& b) { return a.bids == b.bids && a.asks == b.asks; }
bool applied(BookDecision d) { return d == BookDecision::applied_snapshot || d == BookDecision::applied_diff; }
const char* decision_name(BookDecision d) {
    switch (d) {
    case BookDecision::applied_snapshot: return "applied_snapshot";
    case BookDecision::applied_diff: return "applied_diff";
    case BookDecision::stale_snapshot: return "stale_snapshot";
    case BookDecision::stale_diff: return "stale_diff";
    case BookDecision::waiting: return "waiting";
    case BookDecision::gap: return "gap";
    case BookDecision::coverage_lost: return "coverage_lost";
    case BookDecision::crossed: return "crossed";
    case BookDecision::count: break;
    }
    throw std::invalid_argument("unknown book decision");
}

void OrderBook::invalidate() {
    bids_.clear();
    asks_.clear();
    ready_ = false;
    has_state_ = false;
}
void OrderBook::observe_epoch(std::uint64_t epoch) {
    if (epoch_ && epoch < *epoch_) throw std::invalid_argument("connection epoch moved backwards");
    if (!epoch_ || epoch != *epoch_) {
        invalidate();
        revision_ = highest_seen_ = 0;
        epoch_ = epoch;
    }
}
TopFive OrderBook::top() const {
    TopFive result;
    std::size_t i = 0;
    for (const auto& [price, quantity] : bids_) {
        if (i == 5) break;
        result.bids[i++] = {price, quantity};
    }
    i = 0;
    for (const auto& [price, quantity] : asks_) {
        if (i == 5) break;
        result.asks[i++] = {price, quantity};
    }
    return result;
}

BookDecision OrderBook::apply(const DepthEvent& event, std::uint64_t epoch) {
    const bool snapshot = event.kind == DepthKind::snapshot;
    if (event.first > event.last) throw std::invalid_argument("depth U exceeds u");
    validate_side(event.bids, snapshot, true);
    validate_side(event.asks, snapshot, false);
    if (snapshot && !event.bids.empty() && !event.asks.empty()
        && event.bids.front().price >= event.asks.front().price) {
        throw std::invalid_argument("crossed snapshot");
    }
    observe_epoch(epoch);
    if (snapshot) {
        if (event.last < highest_seen_) return BookDecision::stale_snapshot;
        TopFive incoming;
        std::copy(event.bids.begin(), event.bids.end(), incoming.bids.begin());
        std::copy(event.asks.begin(), event.asks.end(), incoming.asks.begin());
        if (has_state_ && event.last == revision_) {
            if (!(incoming == top())) {
                invalidate();
                throw std::invalid_argument("snapshot contradicts book at the same update ID");
            }
            ++consistency_checks_;
        }
        invalidate();
        for (const auto& level : event.bids) bids_[level.price] = level.quantity;
        for (const auto& level : event.asks) asks_[level.price] = level.quantity;
        revision_ = highest_seen_ = event.last;
        has_state_ = true;
        ready_ = event.bids.size() == 5 && event.asks.size() == 5;
        if (ready_) {
            bid_floor_ = event.bids.back().price;
            ask_ceiling_ = event.asks.back().price;
        }
        return BookDecision::applied_snapshot;
    }
    highest_seen_ = std::max(highest_seen_, event.last);
    if (!ready_) return BookDecision::waiting;
    if (event.last <= revision_) {
        if (event.last == revision_) {
            bool checked = false;
            for (const auto& level : event.bids) {
                if (level.price < bid_floor_) continue;
                checked = true;
                const auto found = bids_.find(level.price);
                if ((found == bids_.end() ? 0 : found->second) != level.quantity) {
                    invalidate();
                    throw std::invalid_argument("diff contradicts bid at the same update ID");
                }
            }
            for (const auto& level : event.asks) {
                if (level.price > ask_ceiling_) continue;
                checked = true;
                const auto found = asks_.find(level.price);
                if ((found == asks_.end() ? 0 : found->second) != level.quantity) {
                    invalidate();
                    throw std::invalid_argument("diff contradicts ask at the same update ID");
                }
            }
            if (checked) ++consistency_checks_;
        }
        return BookDecision::stale_diff;
    }
    // last > revision ensures revision + 1 cannot overflow.
    if (event.first > revision_ + 1) {
        invalidate();
        return BookDecision::gap;
    }
    for (const auto& level : event.bids) {
        if (level.price < bid_floor_) continue;
        if (level.quantity == 0) bids_.erase(level.price);
        else bids_[level.price] = level.quantity;
    }
    for (const auto& level : event.asks) {
        if (level.price > ask_ceiling_) continue;
        if (level.quantity == 0) asks_.erase(level.price);
        else asks_[level.price] = level.quantity;
    }
    if (bids_.size() > max_levels || asks_.size() > max_levels) {
        invalidate();
        throw std::invalid_argument("retained book level limit exceeded");
    }
    if (bids_.size() < 5 || asks_.size() < 5) {
        invalidate();
        return BookDecision::coverage_lost;
    }
    if (bids_.begin()->first >= asks_.begin()->first) {
        invalidate();
        return BookDecision::crossed;
    }
    revision_ = event.last;
    return BookDecision::applied_diff;
}

} // namespace binance_capture
