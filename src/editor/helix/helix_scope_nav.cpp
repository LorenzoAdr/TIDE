#include "editor/helix/helix_scope_nav.hpp"

#include <algorithm>
#include <cctype>

#include "editor/bracket_match.hpp"
#include "editor/text_ops.hpp"

namespace tgdb {

namespace {

bool is_blank_line(const std::string& line) {
  return std::all_of(line.begin(), line.end(),
                     [](unsigned char ch) { return std::isspace(ch); });
}

void jump_cursor(EditorBuffer* buffer, int line_0, int col, int visible_lines) {
  if (buffer == nullptr || buffer->lines.empty()) {
    return;
  }
  const int max_line = static_cast<int>(buffer->lines.size()) - 1;
  line_0 = std::max(0, std::min(line_0, max_line));
  const int line_len =
      static_cast<int>(buffer->lines[static_cast<std::size_t>(line_0)].size());
  col = std::max(0, std::min(col, line_len));
  buffer->reset_to_single_cursor(line_0, col);
  if (visible_lines > 0) {
    buffer->scroll = std::max(0, line_0 - visible_lines / 2);
  }
  ensure_scroll_visible(buffer, visible_lines);
}

bool is_function_kind(SymbolKind kind) {
  return kind == SymbolKind::kFunction || kind == SymbolKind::kMethod;
}

bool is_type_kind(SymbolKind kind) {
  return kind == SymbolKind::kNamespace || kind == SymbolKind::kClass ||
         kind == SymbolKind::kStruct;
}

std::vector<const SymbolInfo*> filter_symbols(const std::vector<SymbolInfo>& symbols,
                                              bool (*predicate)(SymbolKind)) {
  std::vector<const SymbolInfo*> filtered;
  filtered.reserve(symbols.size());
  for (const SymbolInfo& sym : symbols) {
    if (predicate(sym.kind)) {
      filtered.push_back(&sym);
    }
  }
  std::sort(filtered.begin(), filtered.end(),
            [](const SymbolInfo* a, const SymbolInfo* b) {
              if (a->line != b->line) {
                return a->line < b->line;
              }
              return a->depth < b->depth;
            });
  return filtered;
}

const SymbolInfo* next_symbol_after(const std::vector<const SymbolInfo*>& symbols,
                                    int line_1based) {
  const SymbolInfo* best = nullptr;
  for (const SymbolInfo* sym : symbols) {
    if (sym->line <= line_1based) {
      continue;
    }
    if (best == nullptr || sym->line < best->line) {
      best = sym;
    }
  }
  return best;
}

const SymbolInfo* prev_symbol_before(const std::vector<const SymbolInfo*>& symbols,
                                     int line_1based) {
  const SymbolInfo* best = nullptr;
  for (const SymbolInfo* sym : symbols) {
    if (sym->line >= line_1based) {
      continue;
    }
    if (best == nullptr || sym->line > best->line) {
      best = sym;
    }
  }
  return best;
}

bool goto_symbol_start(const HelixScopeNavContext& ctx, bool (*predicate)(SymbolKind),
                       bool forward) {
  if (ctx.buffer == nullptr || ctx.symbols == nullptr || ctx.buffer->path.empty()) {
    return false;
  }
  const std::vector<SymbolInfo> symbols = ctx.symbols->symbols_for_file(ctx.buffer->path);
  const std::vector<const SymbolInfo*> filtered = filter_symbols(symbols, predicate);
  if (filtered.empty()) {
    return false;
  }
  const int line_1 = ctx.buffer->primary_line() + 1;
  const SymbolInfo* target =
      forward ? next_symbol_after(filtered, line_1) : prev_symbol_before(filtered, line_1);
  if (target == nullptr) {
    return false;
  }
  jump_cursor(ctx.buffer, std::max(0, target->line - 1), 0, ctx.visible_lines);
  return true;
}

int next_paragraph_line(const EditorBuffer& buffer, int from_line_0) {
  const int total = static_cast<int>(buffer.lines.size());
  if (total == 0) {
    return -1;
  }
  int line = from_line_0 + 1;
  if (line >= total) {
    return -1;
  }
  while (line < total && !is_blank_line(buffer.lines[static_cast<std::size_t>(line)])) {
    ++line;
  }
  while (line < total && is_blank_line(buffer.lines[static_cast<std::size_t>(line)])) {
    ++line;
  }
  if (line >= total) {
    return -1;
  }
  return line;
}

int prev_paragraph_line(const EditorBuffer& buffer, int from_line_0) {
  const int total = static_cast<int>(buffer.lines.size());
  if (total == 0) {
    return -1;
  }
  int line = from_line_0 - 1;
  if (line < 0) {
    return -1;
  }
  while (line >= 0 && is_blank_line(buffer.lines[static_cast<std::size_t>(line)])) {
    --line;
  }
  if (line < 0) {
    return -1;
  }
  while (line > 0 && !is_blank_line(buffer.lines[static_cast<std::size_t>(line - 1)])) {
    --line;
  }
  return line;
}

}  // namespace

bool helix_goto_next_function(const HelixScopeNavContext& ctx) {
  return goto_symbol_start(ctx, is_function_kind, true);
}

bool helix_goto_prev_function(const HelixScopeNavContext& ctx) {
  return goto_symbol_start(ctx, is_function_kind, false);
}

bool helix_goto_next_type(const HelixScopeNavContext& ctx) {
  return goto_symbol_start(ctx, is_type_kind, true);
}

bool helix_goto_prev_type(const HelixScopeNavContext& ctx) {
  return goto_symbol_start(ctx, is_type_kind, false);
}

bool helix_goto_next_paragraph(const HelixScopeNavContext& ctx) {
  if (ctx.buffer == nullptr) {
    return false;
  }
  const int target = next_paragraph_line(*ctx.buffer, ctx.buffer->primary_line());
  if (target < 0) {
    return false;
  }
  jump_cursor(ctx.buffer, target, 0, ctx.visible_lines);
  return true;
}

bool helix_goto_prev_paragraph(const HelixScopeNavContext& ctx) {
  if (ctx.buffer == nullptr) {
    return false;
  }
  const int target = prev_paragraph_line(*ctx.buffer, ctx.buffer->primary_line());
  if (target < 0) {
    return false;
  }
  jump_cursor(ctx.buffer, target, 0, ctx.visible_lines);
  return true;
}

bool helix_goto_block_end(const HelixScopeNavContext& ctx) {
  if (ctx.buffer == nullptr) {
    return false;
  }
  const BracketPairHighlight pair =
      find_enclosing_bracket_pair(*ctx.buffer, ctx.buffer->primary_line(),
                                  ctx.buffer->primary_col(), '{');
  if (!pair.valid) {
    return false;
  }
  jump_cursor(ctx.buffer, pair.line_b, pair.col_b, ctx.visible_lines);
  return true;
}

bool helix_goto_block_start(const HelixScopeNavContext& ctx) {
  if (ctx.buffer == nullptr) {
    return false;
  }
  const BracketPairHighlight pair =
      find_enclosing_bracket_pair(*ctx.buffer, ctx.buffer->primary_line(),
                                  ctx.buffer->primary_col(), '{');
  if (!pair.valid) {
    return false;
  }
  jump_cursor(ctx.buffer, pair.line_a, pair.col_a, ctx.visible_lines);
  return true;
}

}  // namespace tgdb
