#include "editor/helix/helix_keymap.hpp"

#include "i18n/tr.hpp"

namespace tgdb {

namespace {

HelixKeyTrieNode& bind(HelixKeyTrieNode* node, const std::string& key) {
  auto& slot = node->children[key];
  if (!slot) {
    slot = std::make_unique<HelixKeyTrieNode>();
  }
  return *slot;
}

void set_cmd(HelixKeyTrieNode* node, HelixCommand command) {
  node->command = command;
}

void bind_cmd(HelixKeyTrieNode* node, const std::string& key, HelixCommand command) {
  set_cmd(&bind(node, key), command);
}

void build_goto_trie(HelixKeyTrieNode* root) {
  auto& g = bind(root, "g");
  bind_cmd(&g, "g", HelixCommand::kGotoFileStart);
  bind_cmd(&g, "e", HelixCommand::kGotoFileEnd);
  bind_cmd(&g, "h", HelixCommand::kGotoLineStart);
  bind_cmd(&g, "l", HelixCommand::kGotoLineEnd);
  bind_cmd(&g, "d", HelixCommand::kGotoDefinition);
}

void build_view_trie(HelixKeyTrieNode* root) {
  auto& z = bind(root, "z");
  bind_cmd(&z, "u", HelixCommand::kScrollUp);
  bind_cmd(&z, "d", HelixCommand::kScrollDown);
  bind_cmd(&z, "k", HelixCommand::kScrollUp);
  bind_cmd(&z, "j", HelixCommand::kScrollDown);
}

void build_space_trie(HelixKeyTrieNode* root) {
  auto& sp = bind(root, "<space>");
  bind_cmd(&sp, "c", HelixCommand::kToggleComments);
  bind_cmd(&sp, "?", HelixCommand::kShowHelp);
  bind_cmd(&sp, "f", HelixCommand::kOpenQuickFile);
  bind_cmd(&sp, "s", HelixCommand::kOpenSymbolPicker);
}

void build_unimpaired_trie(HelixKeyTrieNode* root) {
  auto& lb = bind(root, "[");
  bind_cmd(&lb, "d", HelixCommand::kGotoPrevDiagnostic);
  bind_cmd(&lb, "f", HelixCommand::kGotoPrevFunction);
  bind_cmd(&lb, "t", HelixCommand::kGotoPrevType);
  bind_cmd(&lb, "p", HelixCommand::kGotoPrevParagraph);
  bind_cmd(&lb, "}", HelixCommand::kGotoBlockStart);
  auto& rb = bind(root, "]");
  bind_cmd(&rb, "d", HelixCommand::kGotoNextDiagnostic);
  bind_cmd(&rb, "f", HelixCommand::kGotoNextFunction);
  bind_cmd(&rb, "t", HelixCommand::kGotoNextType);
  bind_cmd(&rb, "p", HelixCommand::kGotoNextParagraph);
  bind_cmd(&rb, "}", HelixCommand::kGotoBlockEnd);
}

void build_match_trie(HelixKeyTrieNode* root) {
  auto& m = bind(root, "m");
  auto& mi = bind(&m, "i");
  bind_cmd(&mi, "w", HelixCommand::kSelectInnerWord);
  bind_cmd(&mi, "(", HelixCommand::kSelectInnerParen);
  bind_cmd(&mi, "{", HelixCommand::kSelectInnerBrace);
  bind_cmd(&mi, "[", HelixCommand::kSelectInnerSquare);
  bind_cmd(&mi, "m", HelixCommand::kSelectInnerSurround);
  bind_cmd(&mi, "f", HelixCommand::kSelectInnerFunction);
  bind_cmd(&mi, "t", HelixCommand::kSelectInnerType);
  bind_cmd(&mi, "a", HelixCommand::kSelectInnerArgument);
  bind_cmd(&mi, "c", HelixCommand::kSelectInnerComment);
  bind_cmd(&mi, "\"", HelixCommand::kSelectInnerDoubleQuote);
  bind_cmd(&mi, "'", HelixCommand::kSelectInnerSingleQuote);
  bind_cmd(&mi, "`", HelixCommand::kSelectInnerBacktick);
  auto& ma = bind(&m, "a");
  bind_cmd(&ma, "w", HelixCommand::kSelectAroundWord);
  bind_cmd(&ma, "(", HelixCommand::kSelectAroundParen);
  bind_cmd(&ma, "{", HelixCommand::kSelectAroundBrace);
  bind_cmd(&ma, "[", HelixCommand::kSelectAroundSquare);
  bind_cmd(&ma, "m", HelixCommand::kSelectAroundSurround);
  bind_cmd(&ma, "f", HelixCommand::kSelectAroundFunction);
  bind_cmd(&ma, "t", HelixCommand::kSelectAroundType);
  bind_cmd(&ma, "a", HelixCommand::kSelectAroundArgument);
  bind_cmd(&ma, "c", HelixCommand::kSelectAroundComment);
  bind_cmd(&ma, "\"", HelixCommand::kSelectAroundDoubleQuote);
  bind_cmd(&ma, "'", HelixCommand::kSelectAroundSingleQuote);
  bind_cmd(&ma, "`", HelixCommand::kSelectAroundBacktick);
  bind_cmd(&m, "d", HelixCommand::kDeleteInnerWord);
  bind_cmd(&m, "m", HelixCommand::kMatchBrackets);
}

HelixKeyTrieNode build_normal_map() {
  HelixKeyTrieNode root;
  bind_cmd(&root, "h", HelixCommand::kMoveCharLeft);
  bind_cmd(&root, "l", HelixCommand::kMoveCharRight);
  bind_cmd(&root, "j", HelixCommand::kMoveLineDown);
  bind_cmd(&root, "k", HelixCommand::kMoveLineUp);
  bind_cmd(&root, "<left>", HelixCommand::kMoveCharLeft);
  bind_cmd(&root, "<right>", HelixCommand::kMoveCharRight);
  bind_cmd(&root, "<down>", HelixCommand::kMoveLineDown);
  bind_cmd(&root, "<up>", HelixCommand::kMoveLineUp);
  bind_cmd(&root, "w", HelixCommand::kMoveWordForward);
  bind_cmd(&root, "b", HelixCommand::kMoveWordBackward);
  bind_cmd(&root, "e", HelixCommand::kMoveWordEnd);
  bind_cmd(&root, "f", HelixCommand::kFindCharForward);
  bind_cmd(&root, "t", HelixCommand::kTillCharForward);
  bind_cmd(&root, "F", HelixCommand::kFindCharBackward);
  bind_cmd(&root, "T", HelixCommand::kTillCharBackward);
  bind_cmd(&root, "<home>", HelixCommand::kGotoLineStart);
  bind_cmd(&root, "<end>", HelixCommand::kGotoLineEnd);
  bind_cmd(&root, "<pageup>", HelixCommand::kPageUp);
  bind_cmd(&root, "<pagedown>", HelixCommand::kPageDown);
  bind_cmd(&root, "i", HelixCommand::kInsertMode);
  bind_cmd(&root, "a", HelixCommand::kAppendMode);
  bind_cmd(&root, "I", HelixCommand::kInsertLineStart);
  bind_cmd(&root, "A", HelixCommand::kInsertLineEnd);
  bind_cmd(&root, "o", HelixCommand::kOpenBelow);
  bind_cmd(&root, "O", HelixCommand::kOpenAbove);
  bind_cmd(&root, "v", HelixCommand::kSelectMode);
  bind_cmd(&root, "x", HelixCommand::kExtendLineBelow);
  bind_cmd(&root, "d", HelixCommand::kDeleteSelection);
  bind_cmd(&root, "c", HelixCommand::kChangeSelection);
  bind_cmd(&root, "y", HelixCommand::kYank);
  bind_cmd(&root, "p", HelixCommand::kPasteAfter);
  bind_cmd(&root, "P", HelixCommand::kPasteBefore);
  bind_cmd(&root, "u", HelixCommand::kUndo);
  bind_cmd(&root, "U", HelixCommand::kRedo);
  bind_cmd(&root, "/", HelixCommand::kSearch);
  bind_cmd(&root, "n", HelixCommand::kSearchNext);
  bind_cmd(&root, "N", HelixCommand::kSearchPrev);
  bind_cmd(&root, "s", HelixCommand::kSelectAllMatches);
  bind_cmd(&root, "S", HelixCommand::kSplitSelectionOnRegex);
  bind_cmd(&root, "%", HelixCommand::kSelectAll);
  bind_cmd(&root, "X", HelixCommand::kExtendLineBounds);
  bind_cmd(&root, ">", HelixCommand::kIndent);
  bind_cmd(&root, "<", HelixCommand::kUnindent);
  bind_cmd(&root, "=", HelixCommand::kIndent);
  bind_cmd(&root, ":", HelixCommand::kCommandMode);
  bind_cmd(&root, "G", HelixCommand::kGotoLinePrompt);
  bind_cmd(&root, "<esc>", HelixCommand::kNormalMode);
  auto& semi = bind(&root, ";");
  bind_cmd(&semi, "d", HelixCommand::kDeleteCharForward);
  build_goto_trie(&root);
  build_view_trie(&root);
  build_space_trie(&root);
  build_match_trie(&root);
  build_unimpaired_trie(&root);
  return root;
}

HelixKeyTrieNode build_select_map() {
  HelixKeyTrieNode root = build_normal_map();
  bind_cmd(&root, "h", HelixCommand::kExtendCharLeft);
  bind_cmd(&root, "l", HelixCommand::kExtendCharRight);
  bind_cmd(&root, "j", HelixCommand::kExtendLineDown);
  bind_cmd(&root, "k", HelixCommand::kExtendLineUp);
  bind_cmd(&root, "<left>", HelixCommand::kExtendCharLeft);
  bind_cmd(&root, "<right>", HelixCommand::kExtendCharRight);
  bind_cmd(&root, "<down>", HelixCommand::kExtendLineDown);
  bind_cmd(&root, "<up>", HelixCommand::kExtendLineUp);
  bind_cmd(&root, "w", HelixCommand::kExtendWordForward);
  bind_cmd(&root, "b", HelixCommand::kExtendWordBackward);
  bind_cmd(&root, "e", HelixCommand::kExtendWordEnd);
  bind_cmd(&root, "<home>", HelixCommand::kExtendLineStart);
  bind_cmd(&root, "<end>", HelixCommand::kExtendLineEnd);
  bind_cmd(&root, "v", HelixCommand::kNormalMode);
  bind_cmd(&root, "<esc>", HelixCommand::kExitSelectMode);
  bind_cmd(&root, "x", HelixCommand::kExtendLineBelow);
  return root;
}

HelixKeyTrieNode build_insert_map() {
  HelixKeyTrieNode root;
  bind_cmd(&root, "<esc>", HelixCommand::kNormalMode);
  bind_cmd(&root, "<backspace>", HelixCommand::kDeleteCharBackward);
  bind_cmd(&root, "<del>", HelixCommand::kDeleteCharForward);
  bind_cmd(&root, "<ret>", HelixCommand::kInsertNewline);
  bind_cmd(&root, "<tab>", HelixCommand::kIndent);
  return root;
}

const HelixKeyTrieNode& mode_root(HelixMode mode) {
  static const HelixKeyTrieNode kNormal = build_normal_map();
  static const HelixKeyTrieNode kSelect = build_select_map();
  static const HelixKeyTrieNode kInsert = build_insert_map();
  switch (mode) {
    case HelixMode::kInsert:
      return kInsert;
    case HelixMode::kSelect:
      return kSelect;
    case HelixMode::kNormal:
    default:
      return kNormal;
  }
}

const HelixKeyTrieNode* follow_prefix(const HelixKeyTrieNode& root,
                                      const std::vector<std::string>& prefix) {
  const HelixKeyTrieNode* node = &root;
  for (const std::string& key : prefix) {
    const auto it = node->children.find(key);
    if (it == node->children.end() || it->second == nullptr) {
      return nullptr;
    }
    node = it->second.get();
  }
  return node;
}

}  // namespace

const HelixKeyTrieNode& helix_keymap_root(HelixMode mode) {
  return mode_root(mode);
}

HelixKeyLookupResult helix_lookup_key(HelixMode mode, const std::vector<std::string>& prefix,
                                      const std::string& key) {
  HelixKeyLookupResult result;
  const HelixKeyTrieNode* base = follow_prefix(mode_root(mode), prefix);
  if (base == nullptr) {
    return result;
  }
  const auto it = base->children.find(key);
  if (it == base->children.end() || it->second == nullptr) {
    return result;
  }
  result.node = it->second.get();
  if (it->second->command.has_value()) {
    result.kind = HelixKeyLookupResult::Kind::kMatched;
    result.command = *it->second->command;
  } else if (!it->second->children.empty()) {
    result.kind = HelixKeyLookupResult::Kind::kPending;
  }
  return result;
}

std::vector<std::pair<std::string, std::string>> helix_hint_entries(HelixMode mode,
                                                                    const HelixKeyTrieNode* node) {
  std::vector<std::pair<std::string, std::string>> entries;
  if (node == nullptr) {
    return entries;
  }
  for (const auto& [key, child_ptr] : node->children) {
    if (child_ptr == nullptr) {
      continue;
    }
    const HelixKeyTrieNode& child = *child_ptr;
    std::string label = key;
    if (label == "<space>") {
      label = "space";
    } else if (label.size() > 2 && label.front() == '<') {
      label = label.substr(1, label.size() - 2);
    }
    if (child.command.has_value()) {
      entries.emplace_back(label, helix_command_label(*child.command));
    } else if (!child.children.empty()) {
      entries.emplace_back(label, "…");
    }
  }
  return entries;
}

std::string helix_command_label(HelixCommand command) {
  switch (command) {
    case HelixCommand::kMoveCharLeft:
      return i18n::tr("helix.cmd.move_char_left");
    case HelixCommand::kMoveCharRight:
      return i18n::tr("helix.cmd.move_char_right");
    case HelixCommand::kMoveLineUp:
      return i18n::tr("helix.cmd.move_line_up");
    case HelixCommand::kMoveLineDown:
      return i18n::tr("helix.cmd.move_line_down");
    case HelixCommand::kMoveWordForward:
      return i18n::tr("helix.cmd.move_word_forward");
    case HelixCommand::kMoveWordBackward:
      return i18n::tr("helix.cmd.move_word_backward");
    case HelixCommand::kMoveWordEnd:
      return i18n::tr("helix.cmd.move_word_end");
    case HelixCommand::kFindCharForward:
      return i18n::tr("helix.cmd.find_char_forward");
    case HelixCommand::kTillCharForward:
      return i18n::tr("helix.cmd.till_char_forward");
    case HelixCommand::kFindCharBackward:
      return i18n::tr("helix.cmd.find_char_backward");
    case HelixCommand::kTillCharBackward:
      return i18n::tr("helix.cmd.till_char_backward");
    case HelixCommand::kGotoLineStart:
      return i18n::tr("helix.cmd.goto_line_start");
    case HelixCommand::kGotoLineEnd:
      return i18n::tr("helix.cmd.goto_line_end");
    case HelixCommand::kPageUp:
      return i18n::tr("helix.cmd.page_up");
    case HelixCommand::kPageDown:
      return i18n::tr("helix.cmd.page_down");
    case HelixCommand::kHalfPageUp:
      return i18n::tr("helix.cmd.half_page_up");
    case HelixCommand::kHalfPageDown:
      return i18n::tr("helix.cmd.half_page_down");
    case HelixCommand::kGotoFileStart:
      return i18n::tr("helix.cmd.goto_file_start");
    case HelixCommand::kGotoFileEnd:
      return i18n::tr("helix.cmd.goto_file_end");
    case HelixCommand::kGotoLinePrompt:
      return i18n::tr("helix.cmd.goto_line_prompt");
    case HelixCommand::kExtendCharLeft:
      return i18n::tr("helix.cmd.extend_char_left");
    case HelixCommand::kExtendCharRight:
      return i18n::tr("helix.cmd.extend_char_right");
    case HelixCommand::kExtendLineUp:
      return i18n::tr("helix.cmd.extend_line_up");
    case HelixCommand::kExtendLineDown:
      return i18n::tr("helix.cmd.extend_line_down");
    case HelixCommand::kExtendWordForward:
      return i18n::tr("helix.cmd.extend_word_forward");
    case HelixCommand::kExtendWordBackward:
      return i18n::tr("helix.cmd.extend_word_backward");
    case HelixCommand::kExtendWordEnd:
      return i18n::tr("helix.cmd.extend_word_end");
    case HelixCommand::kExtendLineStart:
      return i18n::tr("helix.cmd.extend_line_start");
    case HelixCommand::kExtendLineEnd:
      return i18n::tr("helix.cmd.extend_line_end");
    case HelixCommand::kInsertMode:
      return i18n::tr("helix.cmd.insert_mode");
    case HelixCommand::kAppendMode:
      return i18n::tr("helix.cmd.append_mode");
    case HelixCommand::kInsertLineStart:
      return i18n::tr("helix.cmd.insert_line_start");
    case HelixCommand::kInsertLineEnd:
      return i18n::tr("helix.cmd.insert_line_end");
    case HelixCommand::kOpenBelow:
      return i18n::tr("helix.cmd.open_below");
    case HelixCommand::kOpenAbove:
      return i18n::tr("helix.cmd.open_above");
    case HelixCommand::kNormalMode:
      return i18n::tr("helix.cmd.normal_mode");
    case HelixCommand::kSelectMode:
      return i18n::tr("helix.cmd.select_mode");
    case HelixCommand::kExitSelectMode:
      return i18n::tr("helix.cmd.exit_select_mode");
    case HelixCommand::kDeleteSelection:
      return i18n::tr("helix.cmd.delete_selection");
    case HelixCommand::kChangeSelection:
      return i18n::tr("helix.cmd.change_selection");
    case HelixCommand::kDeleteCharForward:
      return i18n::tr("helix.cmd.delete_char_forward");
    case HelixCommand::kYank:
      return i18n::tr("helix.cmd.yank");
    case HelixCommand::kPasteAfter:
      return i18n::tr("helix.cmd.paste_after");
    case HelixCommand::kPasteBefore:
      return i18n::tr("helix.cmd.paste_before");
    case HelixCommand::kUndo:
      return i18n::tr("helix.cmd.undo");
    case HelixCommand::kRedo:
      return i18n::tr("helix.cmd.redo");
    case HelixCommand::kSearch:
      return i18n::tr("helix.cmd.search");
    case HelixCommand::kSearchNext:
      return i18n::tr("helix.cmd.search_next");
    case HelixCommand::kSearchPrev:
      return i18n::tr("helix.cmd.search_prev");
    case HelixCommand::kSelectAll:
      return i18n::tr("helix.cmd.select_all");
    case HelixCommand::kExtendLineBelow:
      return i18n::tr("helix.cmd.extend_line_below");
    case HelixCommand::kExtendLineBounds:
      return i18n::tr("helix.cmd.extend_line_bounds");
    case HelixCommand::kIndent:
      return i18n::tr("helix.cmd.indent");
    case HelixCommand::kUnindent:
      return i18n::tr("helix.cmd.unindent");
    case HelixCommand::kToggleComments:
      return i18n::tr("helix.cmd.toggle_comments");
    case HelixCommand::kScrollUp:
      return i18n::tr("helix.cmd.scroll_up");
    case HelixCommand::kScrollDown:
      return i18n::tr("helix.cmd.scroll_down");
    case HelixCommand::kShowHelp:
      return i18n::tr("helix.cmd.show_help");
    case HelixCommand::kGotoDefinition:
      return i18n::tr("helix.cmd.goto_definition");
    case HelixCommand::kSelectInnerWord:
      return i18n::tr("helix.cmd.select_inner_word");
    case HelixCommand::kSelectAroundWord:
      return i18n::tr("helix.cmd.select_around_word");
    case HelixCommand::kDeleteInnerWord:
      return i18n::tr("helix.cmd.delete_inner_word");
    case HelixCommand::kSelectInnerParen:
      return i18n::tr("helix.cmd.select_inner_paren");
    case HelixCommand::kSelectAroundParen:
      return i18n::tr("helix.cmd.select_around_paren");
    case HelixCommand::kSelectInnerBrace:
      return i18n::tr("helix.cmd.select_inner_brace");
    case HelixCommand::kSelectAroundBrace:
      return i18n::tr("helix.cmd.select_around_brace");
    case HelixCommand::kSelectInnerSquare:
      return i18n::tr("helix.cmd.select_inner_square");
    case HelixCommand::kSelectAroundSquare:
      return i18n::tr("helix.cmd.select_around_square");
    case HelixCommand::kSelectInnerSurround:
      return i18n::tr("helix.cmd.select_inner_surround");
    case HelixCommand::kSelectAroundSurround:
      return i18n::tr("helix.cmd.select_around_surround");
    case HelixCommand::kSelectInnerFunction:
      return i18n::tr("helix.cmd.select_inner_function");
    case HelixCommand::kSelectAroundFunction:
      return i18n::tr("helix.cmd.select_around_function");
    case HelixCommand::kSelectInnerType:
      return i18n::tr("helix.cmd.select_inner_type");
    case HelixCommand::kSelectAroundType:
      return i18n::tr("helix.cmd.select_around_type");
    case HelixCommand::kSelectInnerArgument:
      return i18n::tr("helix.cmd.select_inner_argument");
    case HelixCommand::kSelectAroundArgument:
      return i18n::tr("helix.cmd.select_around_argument");
    case HelixCommand::kSelectInnerComment:
      return i18n::tr("helix.cmd.select_inner_comment");
    case HelixCommand::kSelectAroundComment:
      return i18n::tr("helix.cmd.select_around_comment");
    case HelixCommand::kSelectInnerDoubleQuote:
      return i18n::tr("helix.cmd.select_inner_double_quote");
    case HelixCommand::kSelectAroundDoubleQuote:
      return i18n::tr("helix.cmd.select_around_double_quote");
    case HelixCommand::kSelectInnerSingleQuote:
      return i18n::tr("helix.cmd.select_inner_single_quote");
    case HelixCommand::kSelectAroundSingleQuote:
      return i18n::tr("helix.cmd.select_around_single_quote");
    case HelixCommand::kSelectInnerBacktick:
      return i18n::tr("helix.cmd.select_inner_backtick");
    case HelixCommand::kSelectAroundBacktick:
      return i18n::tr("helix.cmd.select_around_backtick");
    case HelixCommand::kMatchBrackets:
      return i18n::tr("helix.cmd.match_brackets");
    case HelixCommand::kCommandMode:
      return i18n::tr("helix.cmd.command_mode");
    case HelixCommand::kOpenQuickFile:
      return i18n::tr("helix.cmd.open_quick_file");
    case HelixCommand::kOpenSymbolPicker:
      return i18n::tr("helix.cmd.open_symbol_picker");
    case HelixCommand::kGotoNextDiagnostic:
      return i18n::tr("helix.cmd.goto_next_diagnostic");
    case HelixCommand::kGotoPrevDiagnostic:
      return i18n::tr("helix.cmd.goto_prev_diagnostic");
    case HelixCommand::kGotoNextFunction:
      return i18n::tr("helix.cmd.goto_next_function");
    case HelixCommand::kGotoPrevFunction:
      return i18n::tr("helix.cmd.goto_prev_function");
    case HelixCommand::kGotoNextType:
      return i18n::tr("helix.cmd.goto_next_type");
    case HelixCommand::kGotoPrevType:
      return i18n::tr("helix.cmd.goto_prev_type");
    case HelixCommand::kGotoNextParagraph:
      return i18n::tr("helix.cmd.goto_next_paragraph");
    case HelixCommand::kGotoPrevParagraph:
      return i18n::tr("helix.cmd.goto_prev_paragraph");
    case HelixCommand::kGotoBlockEnd:
      return i18n::tr("helix.cmd.goto_block_end");
    case HelixCommand::kGotoBlockStart:
      return i18n::tr("helix.cmd.goto_block_start");
    case HelixCommand::kSelectAllMatches:
      return i18n::tr("helix.cmd.select_all_matches");
    case HelixCommand::kSplitSelectionOnRegex:
      return i18n::tr("helix.cmd.split_selection_on_regex");
    case HelixCommand::kSplitSelectionOnNewline:
      return i18n::tr("helix.cmd.split_selection_on_newline");
    case HelixCommand::kNone:
    default:
      return "";
  }
}

}  // namespace tgdb
