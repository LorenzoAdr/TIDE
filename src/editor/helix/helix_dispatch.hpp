#pragma once

#include <functional>

#include "app/workspace_model.hpp"
#include "editor/editor_find_state.hpp"
#include "editor/editor_state.hpp"
#include "editor/helix/helix_commands.hpp"
#include "editor/helix/helix_state.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tgdb {

class ISymbolProvider;

struct HelixDispatchContext {
  EditorBuffer* buffer = nullptr;
  HelixEditorState* helix = nullptr;
  EditorFindState* find = nullptr;
  MainLayoutState* layout_state = nullptr;
  FocusManagerState* focus = nullptr;
  FocusRegion panel_focus = FocusRegion::Editor;
  WorkspaceModel* workspace = nullptr;
  int visible_lines = 24;
  int code_width = 80;
  std::function<void()> on_buffer_changed;
  std::function<void()> open_find_bar;
  std::function<void()> open_goto_line;
  std::function<bool(int line, int col, bool declaration)> go_to_symbol;
  std::function<void()> open_quick_file;
  std::function<void()> open_symbol_picker;
  std::function<void()> save_file;
  std::function<void()> request_quit;
  std::function<void()> goto_next_diagnostic;
  std::function<void()> goto_prev_diagnostic;
  ISymbolProvider* symbols = nullptr;
};

bool execute_helix_command(const HelixDispatchContext& ctx, HelixCommand command);

bool dispatch_helix_keys(const HelixDispatchContext& ctx, const ftxui::Event& event);

HelixStatusSnapshot helix_status_snapshot(const HelixEditorState* helix, bool enabled);

int helix_gutter_width(int total_lines, int visible_lines, bool relative);

std::string helix_format_line_number(int line_idx, int primary_line, int width, bool relative);

void sync_helix_layout_status(MainLayoutState* layout_state, const HelixEditorState* helix,
                              bool enabled);

}  // namespace tgdb
