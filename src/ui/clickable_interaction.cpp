#include "ui/clickable_interaction.hpp"

#include <algorithm>

namespace tgdb {

void ClickableInteractionTracker::set_hover(std::string_view id) {
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
  if (!hovered_id_.empty() && predicate(hovered_id_)) {
    hovered_id_.clear();
  }
}

bool ClickableInteractionTracker::is_hovered(std::string_view id) const {
  return !id.empty() && hovered_id_ == id;
}

std::string_view ClickableInteractionTracker::hovered_id() const {
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

void ClickableInteractionTracker::tick() {
  const auto now = std::chrono::steady_clock::now();
  presses_.erase(std::remove_if(presses_.begin(), presses_.end(),
                              [now](const auto& entry) { return entry.second <= now; }),
                 presses_.end());
}

}  // namespace tgdb
