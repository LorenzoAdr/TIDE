#pragma once

#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/clickable_interaction.hpp"
#include "ui/hover_effects.hpp"
#include "ui/main_layout.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

struct HoverTarget {
  std::string_view id;
  const Box* box = nullptr;
};

struct ClickableState {
  bool selected = false;
  bool hovered = false;
  bool pressed = false;
  bool disabled = false;
};

inline std::optional<std::string_view> hit_test_hover(int x, int y,
                                                      std::initializer_list<HoverTarget> targets) {
  for (const HoverTarget& target : targets) {
    if (target.box != nullptr && !target.box->IsEmpty() && target.box->Contain(x, y)) {
      return target.id;
    }
  }
  return std::nullopt;
}

inline void trigger_press(MainLayoutState* layout, std::string_view id) {
  if (layout == nullptr || id.empty()) {
    return;
  }
  layout->clickable.trigger_press(id);
  invalidate_cached_panel_chrome(layout, id);
  layout->request_ui_tick = true;
}

inline void trigger_press(MainLayoutState* layout, const std::string& id) {
  trigger_press(layout, std::string_view(id));
}

inline bool update_panel_hover(MainLayoutState* layout, int x, int y,
                               std::initializer_list<HoverTarget> targets,
                               const std::function<bool(std::string_view)>& owns_hover) {
  if (layout == nullptr || !chrome_hover_allowed(layout)) {
    return false;
  }

  const auto hit = hit_test_hover(x, y, targets);
  if (hit.has_value()) {
    const std::string_view before = layout->clickable.hovered_id();
    layout->clickable.set_hover(*hit);
    return apply_hover_repaint(layout, before);
  }

  const std::string_view before = layout->clickable.hovered_id();
  layout->clickable.clear_hover_if(owns_hover);
  return apply_hover_repaint(layout, before);
}

inline std::optional<int> local_row_in_box(const Box& box, int x, int y) {
  if (box.IsEmpty() || !box.Contain(x, y)) {
    return std::nullopt;
  }
  return y - box.y_min;
}

inline Element StyleListRow(Element row, bool selected, bool hovered, bool pressed) {
  if (!hover_effects_enabled()) {
    hovered = false;
  }
  if (pressed) {
    return row | bold | inverted | bgcolor(theme::TabPressed());
  }
  if (hovered) {
    if (selected) {
      return row | bold | inverted | bgcolor(theme::TabActive());
    }
    return row | bold | bgcolor(theme::TabHover());
  }
  if (selected) {
    return row | inverted | bold;
  }
  return row;
}

inline Element StyleClickable(Element base, ClickableState state) {
  if (!hover_effects_enabled()) {
    state.hovered = false;
  }
  if (state.disabled) {
    return base | dim;
  }
  if (state.pressed) {
    return base | bold | inverted | bgcolor(theme::TabPressed());
  }
  if (state.hovered) {
    const Color bg = state.selected ? theme::TabActive() : theme::TabHover();
    return base | bold | color(theme::TitleText()) | bgcolor(bg);
  }
  if (state.selected) {
    return base | bold | color(theme::TitleText()) | bgcolor(theme::TabActive());
  }
  return base | color(theme::FileText()) | bgcolor(theme::TabIdle());
}

inline Element MakeTabButton(const std::string& label, bool selected, bool hovered, bool pressed,
                             Box* box) {
  Element tab = text(" " + label + " ") | center | size(HEIGHT, EQUAL, 1);
  tab = StyleClickable(std::move(tab), {selected, hovered, pressed, false});
  return tab | flex | reflect(*box);
}

inline Element MakeToolbarButton(Element content, bool hovered, bool pressed, bool disabled,
                                 Box* box, bool compact = false) {
  if (!hover_effects_enabled()) {
    hovered = false;
  }
  Element btn = compact
                    ? (std::move(content) | center | size(HEIGHT, EQUAL, 1))
                    : (hbox({text("  "), std::move(content), text("  ")}) | center |
                       size(HEIGHT, EQUAL, 1));
  if (disabled) {
    btn = btn | dim | bgcolor(theme::TabIdle());
  } else if (pressed) {
    btn = btn | bold | inverted | bgcolor(theme::TabPressed());
  } else if (hovered) {
    btn = btn | bold | bgcolor(theme::TabHover());
  } else {
    btn = btn | bgcolor(theme::TabIdle());
  }
  return btn | reflect(*box);
}

struct SplitToolbarButtonBoxes {
  Box main;
  Box arrow;
};

inline Element MakeSplitToolbarButton(Element main_content, Element arrow_content,
                                      bool main_hovered, bool main_pressed, bool arrow_hovered,
                                      bool arrow_pressed, bool disabled,
                                      SplitToolbarButtonBoxes* boxes) {
  return hbox({
      MakeToolbarButton(std::move(main_content), main_hovered, main_pressed, disabled,
                        &boxes->main, true),
      MakeToolbarButton(std::move(arrow_content), arrow_hovered, arrow_pressed, disabled,
                        &boxes->arrow, true),
  });
}

inline bool interaction_active(const MainLayoutState* layout, std::string_view id) {
  return layout != nullptr &&
         ((hover_effects_enabled() && layout->clickable.is_hovered(id)) ||
          layout->clickable.is_pressed(id));
}

}  // namespace tgdb
