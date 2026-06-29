#include "ui/cursor_blink.hpp"

#include <chrono>

namespace tgdb::cursor_blink {

namespace {

constexpr int kHalfPeriodMs = 530;

int64_t steady_now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool visible_ = true;
int64_t last_toggle_ms_ = 0;

}  // namespace

void tick() {
  const int64_t now = steady_now_ms();
  if (now - last_toggle_ms_ >= kHalfPeriodMs) {
    visible_ = !visible_;
    last_toggle_ms_ = now;
  }
}

bool visible() { return visible_; }

}  // namespace tgdb::cursor_blink
