#include "ui/mouse_velocity_tracker.hpp"

#include <algorithm>

namespace tgdb {

bool MouseVelocityTracker::on_mouse_move(int x, int y, int64_t now_ms) {
  if (last_ms_ <= 0) {
    last_x_ = x;
    last_y_ = y;
    last_ms_ = now_ms;
    return false;
  }
  const int64_t dt_ms = std::max<int64_t>(1, now_ms - last_ms_);
  const double dx = static_cast<double>(x - last_x_);
  const double dy = static_cast<double>(y - last_y_);
  const double speed = std::sqrt(dx * dx + dy * dy) * 1000.0 / static_cast<double>(dt_ms);

  const SpeedZone prev = zone_;
  if (zone_ == SpeedZone::kLow) {
    if (speed >= kEnterHighPxPerSec) {
      zone_ = SpeedZone::kHigh;
    }
  } else if (speed <= kExitLowPxPerSec) {
    zone_ = SpeedZone::kLow;
  }

  last_x_ = x;
  last_y_ = y;
  last_ms_ = now_ms;

  return prev == SpeedZone::kHigh && zone_ == SpeedZone::kLow;
}

}  // namespace tgdb
