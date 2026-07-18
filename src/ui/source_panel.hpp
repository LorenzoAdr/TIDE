#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "app/debug_model.hpp"
#include "symbols/symbol_provider.hpp"

#include "ui/focus_manager.hpp"

namespace tuide {

struct MainLayoutState;

using CommandCallback = std::function<void(const struct UiCommand&)>;

struct SourceDebugHoverState {
  int line = -1;
  int col = -1;
  int anchor_x = 0;
  int anchor_y = 0;
  int64_t dwell_start_ms = 0;
  std::string fetch_key;
  std::string pending_expression;
  bool waiting_evaluate = false;
  std::string title;
  std::vector<std::string> body_lines;
  bool visible = false;
};

struct SourceViewState {
  std::vector<std::string> lines;
  int scroll = 0;
  int cursor_line = 0;
  SourceDebugHoverState debug_hover;
};

void clear_source_debug_hover(SourceDebugHoverState* hover);

void ToggleBreakpointAtFile(DebugModel* model, const std::string& file, int line,
                            CommandCallback on_command);
void ToggleBreakpointAtLine(DebugModel* model, int line, CommandCallback on_command);

ftxui::Component MakeSourcePanel(DebugModel* model, SourceViewState* view_state,
                                 CommandCallback on_command, FocusManagerState* focus,
                                 MainLayoutState* layout_state,
                                 std::shared_ptr<ISymbolProvider> symbols);

}  // namespace tuide
