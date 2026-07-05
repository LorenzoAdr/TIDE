#pragma once

#include "editor/editor_state.hpp"
#include "symbols/symbol_provider.hpp"

namespace tgdb {

struct HelixScopeNavContext {
  EditorBuffer* buffer = nullptr;
  ISymbolProvider* symbols = nullptr;
  int visible_lines = 24;
};

bool helix_goto_next_function(const HelixScopeNavContext& ctx);
bool helix_goto_prev_function(const HelixScopeNavContext& ctx);
bool helix_goto_next_type(const HelixScopeNavContext& ctx);
bool helix_goto_prev_type(const HelixScopeNavContext& ctx);
bool helix_goto_next_paragraph(const HelixScopeNavContext& ctx);
bool helix_goto_prev_paragraph(const HelixScopeNavContext& ctx);
bool helix_goto_block_end(const HelixScopeNavContext& ctx);
bool helix_goto_block_start(const HelixScopeNavContext& ctx);

}  // namespace tgdb
