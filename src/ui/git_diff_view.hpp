#pragma once

#include <string>
#include <vector>

#include "editor/text_search.hpp"
#include "ftxui/dom/elements.hpp"
#include "git/git_diff.hpp"

namespace tgdb {

struct GitDiffViewRenderResult {
  ftxui::Element body = ftxui::text("");
  int rendered_lines = 0;
};

GitDiffViewRenderResult render_git_diff_viewport(
    const std::vector<SideBySideDiffRow>& rows, int scroll, int visible, int scroll_col,
    int code_width, const std::vector<TextMatch>* find_matches, int active_find_line);

}  // namespace tgdb
