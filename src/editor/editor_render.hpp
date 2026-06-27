#pragma once

#include <vector>

#include "editor/editor_state.hpp"
#include "editor/text_search.hpp"
#include "ftxui/dom/elements.hpp"

namespace tgdb {

struct EditorDecoration {
  int start_col = 0;
  int end_col = 0;
  enum class Kind { FindMatch, Selection, PrimaryCaret, SecondaryCaret } kind =
      Kind::Selection;
};

void collect_find_decorations(int line_index, const std::vector<TextMatch>& matches,
                              std::vector<EditorDecoration>* out);

void collect_line_decorations(int line_index, const EditorBuffer& buffer, bool editor_focused,
                              std::vector<EditorDecoration>* out);

ftxui::Element RenderEditorLine(const std::string& line, int line_index,
                               const EditorBuffer& buffer, bool editor_focused,
                               const std::vector<TextMatch>* find_matches = nullptr);

}  // namespace tgdb
