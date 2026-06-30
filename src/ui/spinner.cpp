#include "ui/spinner.hpp"

#include <chrono>

namespace tgdb::spinner {

namespace {

constexpr int kFrameMs = 120;

int64_t steady_now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

std::string glyph() {
  static const char* kFrames[] = {"|", "/", "-", "\\"};
  const int idx = static_cast<int>((steady_now_ms() / kFrameMs) % 4);
  return kFrames[idx];
}

}  // namespace tgdb::spinner
