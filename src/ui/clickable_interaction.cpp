#include "ui/clickable_interaction.hpp"

#include <algorithm>

#include "ui/hover_effects.hpp"

namespace tgdb {

void ClickableInteractionTracker::set_hover(std::string_view id) {
  if (!hover_effects_enabled()) {
    return;
  }
  const std::string next(id);
  if (hovered_id_ == next) {
    return;
  }
  hovered_id_ = next;
}

void ClickableInteractionTracker::clear_hover() {
  hovered_id_.clear();
}

void ClickableInteractionTracker::clear_hover_if(
    const std::function<bool(std::string_view)>& predicate) {
  if (!hover_effects_enabled()) {
    return;
  }
  if (!hovered_id_.empty() && predicate(hovered_id_)) {
    hovered_id_.clear();
  }
}

bool ClickableInteractionTracker::is_hovered(std::string_view id) const {
  if (!hover_effects_enabled()) {
    return false;
  }
  return !id.empty() && hovered_id_ == id;
}

std::string_view ClickableInteractionTracker::hovered_id() const {
  if (!hover_effects_enabled()) {
    return {};
  }
  return hovered_id_;
}

void ClickableInteractionTracker::trigger_press(std::string_view id,
                                                std::chrono::milliseconds duration) {
  if (id.empty()) {
    return;
  }
  const auto expiry = std::chrono::steady_clock::now() + duration;
  const std::string key(id);
  for (auto& entry : presses_) {
    if (entry.first == key) {
      entry.second = expiry;
      return;
    }
  }
  presses_.emplace_back(key, expiry);
}

bool ClickableInteractionTracker::is_pressed(std::string_view id) const {
  if (id.empty()) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  for (const auto& entry : presses_) {
    if (entry.first == id && entry.second > now) {
      return true;
    }
  }
  return false;
}

void ClickableInteractionTracker::trigger_press(const std::string& id,
                                                std::chrono::milliseconds duration) {
  trigger_press(std::string_view(id), duration);
}

bool ClickableInteractionTracker::is_hovered(const std::string& id) const {
  return is_hovered(std::string_view(id));
}

bool ClickableInteractionTracker::is_pressed(const std::string& id) const {
  return is_pressed(std::string_view(id));
}

bool ClickableInteractionTracker::has_active_presses() const {
  const auto now = std::chrono::steady_clock::now();
  for (const auto& entry : presses_) {
    if (entry.second > now) {
      return true;
    }
  }
  return false;
}

bool ClickableInteractionTracker::tick() {
  const auto now = std::chrono::steady_clock::now();
  const std::size_t before = presses_.size();
  presses_.erase(std::remove_if(presses_.begin(), presses_.end(),
                                [now](const auto& entry) { return entry.second <= now; }),
                 presses_.end());
  return presses_.size() != before;
}

}  // namespace tgdb
