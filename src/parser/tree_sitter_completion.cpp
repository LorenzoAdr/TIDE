#include "parser/tree_sitter_completion.hpp"

#include <cctype>
#include <sstream>
#include <unordered_set>

#include "parser/tree_sitter_locals.hpp"
#include "parser/tree_sitter_symbols.hpp"

namespace tgdb {

namespace {

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string prefix_at_cursor(const std::string& line, int character) {
  if (character <= 0) {
    return {};
  }
  const int clamped = std::min(character, static_cast<int>(line.size()));
  int start = clamped;
  while (start > 0 && is_ident_char(line[static_cast<std::size_t>(start - 1)])) {
    --start;
  }
  return line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(clamped - start));
}

}  // namespace

std::vector<CompletionItem> local_completions_at(TSNode root, const std::string& source,
                                                   const CompletionParams& params) {
  std::vector<CompletionItem> items;
  if (ts_node_is_null(root) || params.text.empty()) {
    return items;
  }

  std::istringstream input(params.text);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  if (lines.empty()) {
    return items;
  }
  if (params.line < 0 || params.line >= static_cast<int>(lines.size())) {
    return items;
  }

  const std::string prefix = prefix_at_cursor(lines[static_cast<std::size_t>(params.line)],
                                              params.character);
  std::unordered_set<std::string> names;
  for (const std::string& name :
       visible_local_names_at(root, source, params.line, params.character)) {
    names.insert(name);
  }

  for (const SymbolInfo& sym : extract_symbols_from_tree(root, source, params.path)) {
    const auto pos = sym.name.find(' ');
    const std::string bare = pos == std::string::npos ? sym.name : sym.name.substr(pos + 1);
    if (!bare.empty()) {
      names.insert(bare);
    }
  }

  for (const std::string& name : names) {
    if (!prefix.empty() && name.compare(0, prefix.size(), prefix) != 0) {
      continue;
    }
    CompletionItem item;
    item.label = name;
    item.insert_text = name;
    item.kind = SymbolKind::kVariable;
    item.file = params.path;
    items.push_back(std::move(item));
  }

  std::sort(items.begin(), items.end(),
            [](const CompletionItem& a, const CompletionItem& b) { return a.label < b.label; });
  return items;
}

}  // namespace tgdb
