#pragma once

#include "editor/bracket_match.hpp"
#include "editor/editor_state.hpp"
#include "symbols/symbol_provider.hpp"

namespace tgdb {

struct HelixTextObjectContext {
  EditorBuffer* buffer = nullptr;
  ISymbolProvider* symbols = nullptr;
};

bool helix_select_inner_function(const HelixTextObjectContext& ctx);
bool helix_select_around_function(const HelixTextObjectContext& ctx);
bool helix_select_inner_type(const HelixTextObjectContext& ctx);
bool helix_select_around_type(const HelixTextObjectContext& ctx);
bool helix_select_inner_argument(const HelixTextObjectContext& ctx);
bool helix_select_around_argument(const HelixTextObjectContext& ctx);
bool helix_select_inner_comment(const HelixTextObjectContext& ctx);
bool helix_select_around_comment(const HelixTextObjectContext& ctx);
bool helix_select_inner_quote(const HelixTextObjectContext& ctx, char quote_ch);
bool helix_select_around_quote(const HelixTextObjectContext& ctx, char quote_ch);

void helix_apply_delimited_selection(EditorBuffer* buffer, const TextSpan& span, bool around,
                                     int inner_skip_start, int inner_skip_end);

}  // namespace tgdb
