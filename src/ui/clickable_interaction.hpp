#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tgdb {

class ClickableInteractionTracker {
 public:
  void set_hover(std::string_view id);
  void clear_hover();
  void clear_hover_if(const std::function<bool(std::string_view)>& predicate);
  bool is_hovered(std::string_view id) const;
  std::string_view hovered_id() const;

  void trigger_press(std::string_view id,
                     std::chrono::milliseconds duration = std::chrono::milliseconds(120));
  void trigger_press(const std::string& id,
                     std::chrono::milliseconds duration = std::chrono::milliseconds(120));
  bool is_hovered(const std::string& id) const;
  bool is_pressed(std::string_view id) const;
  bool is_pressed(const std::string& id) const;
  void tick();

 private:
  std::string hovered_id_;
  std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> presses_;
};

}  // namespace tgdb
