#include "ui/right_sidebar_panel.hpp"

#include <array>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kTabCount = 2;

Element make_tab_button(const std::string& label, bool selected, Box* box) {
  Element tab = text(" " + label + " ") | center | size(HEIGHT, EQUAL, 1);
  if (selected) {
    tab = tab | bold | color(theme::Header()) | bgcolor(theme::TabActive());
  } else {
    tab = tab | color(theme::Muted()) | bgcolor(theme::TabIdle());
  }
  return tab | flex | reflect(*box);
}

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
      tab_row.push_back(make_tab_button(titles[i], selected, &tab_boxes_[i]));
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
      return switch_tab(state_, 0, layout_state_);
    }
    if (event == Event::Character('2')) {
      return switch_tab(state_, 1, layout_state_);
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      for (int i = 0; i < kTabCount; ++i) {
        if (tab_boxes_[i].Contain(m.x, m.y)) {
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
    for (std::size_t i = 0; i < children_.size(); ++i) {
      if (children_[i].get() == child) {
        if (state_ != nullptr) {
          state_->selected_tab = static_cast<int>(i);
        }
        return;
      }
    }
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
