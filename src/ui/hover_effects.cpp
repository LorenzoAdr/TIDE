#include "ui/hover_effects.hpp"

#include <atomic>

namespace tuide {

namespace {

std::atomic<bool> g_animations_enabled{true};

}  // namespace

void set_animations_enabled(bool enabled) { g_animations_enabled.store(enabled); }

bool animations_enabled() { return g_animations_enabled.load(); }

bool hover_effects_enabled() { return animations_enabled(); }

bool editor_scope_effects_enabled(bool scope_highlight_enabled) {
  return animations_enabled() && scope_highlight_enabled;
}

}  // namespace tuide
