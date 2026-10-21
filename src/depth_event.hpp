#pragma once
#include "envelope.hpp"
#include "order_book.hpp"

namespace binance_capture {
DepthEvent decode_depth_event(const MarketEvent& event);
}
