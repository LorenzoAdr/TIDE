#include "ui/right_sidebar_panel.hpp"

#include <array>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/clickable.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kTabCount = 2;

constexpr std::array<std::string_view, kTabCount> kTabIds = {press_id::kSidebarTabOutline,
                                                             press_id::kSidebarTabSearch};

bool switch_tab(RightSidebarState* state, int tab, MainLayoutState* layout_state) {
  if (state == nullptr || tab < 0 || tab >= kTabCount) {
    return false;
  }
  state->selected_tab = tab;
  if (layout_state == nullptr) {
    return true;
  }
  switch (layout_state->text_input_focus) {
    case TextInputFocus::SearchQuery:
    case TextInputFocus::SearchReplace:
    case TextInputFocus::SearchPath:
    case TextInputFocus::SearchExclude:
      if (tab != 1) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      break;
    default:
      break;
  }
  return true;
}

bool handle_sidebar_tab_hover(MainLayoutState* layout_state,
                              const std::array<Box, kTabCount>& tab_boxes, const Mouse& mouse) {
  if (layout_state == nullptr || mouse.motion != Mouse::Moved) {
    return false;
  }
  return update_panel_hover(
      layout_state, mouse.x, mouse.y,
      {{press_id::kSidebarTabOutline, &tab_boxes[0]},
       {press_id::kSidebarTabSearch, &tab_boxes[1]}},
      press_id::is_sidebar_tab_hover);
}

class RightSidebarLayout : public ComponentBase {
 public:
  RightSidebarLayout(Component outline, Component search, RightSidebarState* state,
                     MainLayoutState* layout_state)
      : state_(state), layout_state_(layout_state) {
    tab_boxes_.fill(Box{});
    Add(std::move(outline));
    Add(std::move(search));
  }

  Element OnRender() override {
    Elements tab_row;
    const std::array<std::string, kTabCount> titles = {"Outline", "Buscar"};
    for (int i = 0; i < kTabCount; ++i) {
      const bool selected = state_ != nullptr && state_->selected_tab == i;
      tab_row.push_back(MakeTabButton(
          titles[i], selected,
          layout_state_ != nullptr && layout_state_->clickable.is_hovered(kTabIds[i]),
          layout_state_ != nullptr && layout_state_->clickable.is_pressed(kTabIds[i]),
          &tab_boxes_[i]));
    }

    Component active = ActiveChild();
    Element body = active ? active->Render() : text("");

    return vbox({
               hbox(std::move(tab_row)),
               separator(),
               std::move(body) | flex,
           }) |
           flex | bgcolor(theme::PanelBg());
  }

  bool OnEvent(Event event) override {
    if (state_ == nullptr) {
      return false;
    }

    if (event == Event::Character('1')) {
      trigger_press(layout_state_, press_id::kSidebarTabOutline);
      return switch_tab(state_, 0, layout_state_);
    }
    if (event == Event::Character('2')) {
      trigger_press(layout_state_, press_id::kSidebarTabSearch);
      return switch_tab(state_, 1, layout_state_);
    }

    if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
      handle_sidebar_tab_hover(layout_state_, tab_boxes_, event.mouse());
      return false;
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      for (int i = 0; i < kTabCount; ++i) {
        if (tab_boxes_[i].Contain(m.x, m.y)) {
          trigger_press(layout_state_, kTabIds[i]);
          return switch_tab(state_, i, layout_state_);
        }
      }
    }

    Component active = ActiveChild();
    if (active && active->OnEvent(event)) {
      return true;
    }
    return false;
  }

  bool Focusable() const override {
    if (children_.empty() || state_ == nullptr) {
      return false;
    }
    const int index =
        std::max(0, std::min(state_->selected_tab, static_cast<int>(children_.size()) - 1));
    const Component& active = children_[static_cast<std::size_t>(index)];
    return active && active->Focusable();
  }

  Component ActiveChild() override {
    if (children_.empty() || state_ == nullptr) {
      return nullptr;
    }
    const int index =
        std::max(0, std::min(state_->selected_tab, static_cast<int>(children_.size()) - 1));
    return children_[static_cast<std::size_t>(index)];
  }

  void SetActiveChild(ComponentBase* child) override {
    (void)child;
  }

 private:
  RightSidebarState* state_;
  MainLayoutState* layout_state_;
  std::array<Box, kTabCount> tab_boxes_;
};

}  // namespace

Component MakeRightSidebarPanel(Component outline, Component search, RightSidebarState* state,
                                MainLayoutState* layout_state) {
  return WrapFocusable(Make<RightSidebarLayout>(std::move(outline), std::move(search), state,
                                                layout_state));
}

}  // namespace tgdb
