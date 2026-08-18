#include "ui/status_language_popover.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/language_override.hpp"
#include "ftxui/component/mouse.hpp"
#include "i18n/tr.hpp"
#include "lsp/lsp_uri.hpp"
#include "ui/clickable.hpp"
#include "ui/main_layout.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace tuide {

using namespace ftxui;

namespace {

constexpr int kMaxVisibleLanguageRows = 20;

std::string radio_row_label(bool selected, const std::string& label) {
  return std::string(selected ? " ● " : " ○ ") + label + " ";
}

bool language_popover_hover_id(std::string_view id) {
  return id == press_id::kStatusLanguageAuto ||
         id.rfind(press_id::kStatusLanguageItemPrefix, 0) == 0;
}

int visible_language_count(const StatusLanguagePopoverState* popover) {
  if (popover == nullptr) {
    return 0;
  }
  const int total = static_cast<int>(language_choices().size());
  return std::min(total, kMaxVisibleLanguageRows);
}

void clamp_language_scroll(StatusLanguagePopoverState* popover) {
  if (popover == nullptr) {
    return;
  }
  const int total = static_cast<int>(language_choices().size());
  const int visible = visible_language_count(popover);
  const int max_scroll = std::max(0, total - visible);
  popover->scroll = std::max(0, std::min(popover->scroll, max_scroll));
}

}  // namespace

Element RenderStatusLanguagePopoverOverlay(StatusLanguagePopoverState* popover,
                                           MainLayoutState* layout_state, Element base,
                                           const Box& anchor_box,
                                           const std::string& active_path) {
  if (popover == nullptr || !popover->open || anchor_box.IsEmpty()) {
    return base;
  }

  const auto& choices = language_choices();
  clamp_language_scroll(popover);
  const int visible = visible_language_count(popover);
  popover->language_row_boxes.resize(static_cast<std::size_t>(visible));

  const bool has_override = path_has_language_override(active_path);
  const std::string effective = active_path.empty() ? std::string{} : language_id_for_path(active_path);

  const auto row = [&](const std::string& label, bool selected, std::string_view row_id, Box* box) {
    const bool hovered =
        layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
    const bool pressed =
        layout_state != nullptr && layout_state->clickable.is_pressed(row_id);
    Element line = text(radio_row_label(selected, label)) | color(theme::Header()) |
                   bgcolor(theme::PanelBg());
    line = StyleListRow(std::move(line), selected, hovered, pressed);
    return line | reflect(*box);
  };

  Elements rows;
  rows.push_back(text(" " + i18n::tr("status.language.title") + " ") | bold |
                 color(theme::Accent()) | bgcolor(theme::PanelBg()));
  rows.push_back(separator() | color(theme::AccentDim()));
  rows.push_back(row(i18n::tr("status.language.auto"), !has_override && !active_path.empty(),
                     press_id::kStatusLanguageAuto, &popover->auto_row_box));

  for (int i = 0; i < visible; ++i) {
    const int idx = popover->scroll + i;
    if (idx < 0 || idx >= static_cast<int>(choices.size())) {
      break;
    }
    const auto& choice = choices[static_cast<std::size_t>(idx)];
    const std::string row_id =
        std::string(press_id::kStatusLanguageItemPrefix) + std::to_string(idx);
    const bool selected = has_override && effective == choice.id;
    rows.push_back(row(i18n::tr(choice.label_i18n_key), selected, row_id,
                       &popover->language_row_boxes[static_cast<std::size_t>(i)]));
  }

  Element menu = vbox(std::move(rows)) | border | bgcolor(theme::PanelBg()) |
                 reflect(popover->menu_box);

  const int popover_rows = 3 + visible;  // title + sep + auto + langs
  const int y_top = std::max(0, anchor_box.y_min - popover_rows - 1);
  const int x_left = anchor_box.x_min;

  return dbox({
      std::move(base),
      vbox({
          filler() | size(HEIGHT, EQUAL, y_top),
          hbox({
              filler() | size(WIDTH, EQUAL, x_left),
              std::move(menu) | clear_under,
              filler(),
          }),
          filler(),
      }) | flex,
  });
}

bool HandleStatusLanguagePopoverMouse(StatusLanguagePopoverState* popover,
                                      MainLayoutState* layout_state, const Box& anchor_box,
                                      Event event, const std::string& active_path) {
  if (popover == nullptr || layout_state == nullptr || !popover->open || !event.is_mouse()) {
    return false;
  }

  const Mouse& mouse = event.mouse();
  const auto& choices = language_choices();
  clamp_language_scroll(popover);
  const int visible = visible_language_count(popover);

  if (mouse.motion == Mouse::Moved) {
    static thread_local std::vector<std::string> id_storage;
    id_storage.clear();
    id_storage.reserve(static_cast<std::size_t>(visible));
    std::vector<HoverTarget> hits;
    hits.reserve(static_cast<std::size_t>(visible) + 1);
    hits.push_back({press_id::kStatusLanguageAuto, &popover->auto_row_box});
    for (int i = 0; i < visible; ++i) {
      const int idx = popover->scroll + i;
      id_storage.push_back(std::string(press_id::kStatusLanguageItemPrefix) +
                           std::to_string(idx));
      hits.push_back({id_storage.back(), &popover->language_row_boxes[static_cast<std::size_t>(i)]});
    }
    return update_panel_hover(layout_state, mouse.x, mouse.y, hits, language_popover_hover_id);
  }

  if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
    if (!popover->menu_box.Contain(mouse.x, mouse.y)) {
      return false;
    }
    if (mouse.button == Mouse::WheelUp) {
      popover->scroll = std::max(0, popover->scroll - 1);
    } else {
      popover->scroll += 1;
      clamp_language_scroll(popover);
    }
    UI_WAKE(layout_state, "wake");
    return true;
  }

  if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) {
    return false;
  }

  auto apply_choice = [&](const std::optional<std::string>& language_id) {
    if (layout_state->status_set_file_language) {
      layout_state->status_set_file_language(language_id);
    }
    popover->open = false;
    layout_state->clickable.clear_hover_if(language_popover_hover_id);
    UI_WAKE(layout_state, "wake");
  };

  if (popover->auto_row_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusLanguageAuto);
    apply_choice(std::nullopt);
    return true;
  }

  for (int i = 0; i < visible; ++i) {
    if (!popover->language_row_boxes[static_cast<std::size_t>(i)].Contain(mouse.x, mouse.y)) {
      continue;
    }
    const int idx = popover->scroll + i;
    if (idx < 0 || idx >= static_cast<int>(choices.size())) {
      return true;
    }
    const std::string row_id =
        std::string(press_id::kStatusLanguageItemPrefix) + std::to_string(idx);
    trigger_press(layout_state, row_id);
    apply_choice(std::string(choices[static_cast<std::size_t>(idx)].id));
    return true;
  }

  if (popover->menu_box.Contain(mouse.x, mouse.y)) {
    return true;
  }
  if (anchor_box.Contain(mouse.x, mouse.y)) {
    popover->open = false;
    layout_state->clickable.clear_hover_if(language_popover_hover_id);
    UI_WAKE(layout_state, "wake");
    return true;
  }

  popover->open = false;
  layout_state->clickable.clear_hover_if(language_popover_hover_id);
  UI_WAKE(layout_state, "wake");
  return true;
}

bool HandleStatusLanguagePopoverKeys(StatusLanguagePopoverState* popover, Event event) {
  if (popover == nullptr || !popover->open) {
    return false;
  }
  if (event == Event::Escape) {
    popover->open = false;
    return true;
  }
  return false;
}

}  // namespace tuide
