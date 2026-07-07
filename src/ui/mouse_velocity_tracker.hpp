#pragma once

#include <cmath>
#include <cstdint>

namespace tgdb {

class MouseVelocityTracker {
 public:
  bool on_mouse_move(int x, int y, int64_t now_ms);

 private:
  enum class SpeedZone { kLow, kHigh };

  int last_x_ = -1;
  int last_y_ = -1;
  int64_t last_ms_ = 0;
  SpeedZone zone_ = SpeedZone::kLow;

  static constexpr double kEnterHighPxPerSec = 400.0;
  static constexpr double kExitLowPxPerSec = 150.0;
};

}  // namespace tgdb
