#include "parser/tree_sitter_tags.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#include "parser/tree_sitter_ast_utils.hpp"
#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_language.hpp"
#include "parser/tree_sitter_symbols.hpp"
#include "symbols/symbol_utils.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace fs = std::filesystem;

namespace tuide {
namespace {

bool is_identifier_node_type(const char* type) {
  if (type == nullptr) {
    return false;
  }
  return std::strcmp(type, "identifier") == 0 || std::strcmp(type, "field_identifier") == 0 ||
         std::strcmp(type, "type_identifier") == 0 ||
         std::strcmp(type, "namespace_identifier") == 0 || std::strcmp(type, "property_identifier") == 0 ||
         std::strcmp(type, "word") == 0;
}

bool is_noisy_ident(const std::string& name) {
  if (name.size() < 3) {
    return true;
  }
  static const std::unordered_set<std::string> kNoise = {
      "std",  "int",  "char", "bool", "void", "auto", "this", "true", "false", "null",
      "NULL", "string", "size", "data", "type", "info", "item", "node", "next", "prev",
      "self", "args", "argv", "argc", "ret",  "out",  "err",  "tmp",  "ptr",  "len",
      "str",  "msg",  "key",  "val",  "value", "index", "count", "begin", "end",
  };
  return kNoise.count(name) > 0;
}

std::string source_line_1based(const std::string& source, int line_1) {
  if (line_1 <= 0 || source.empty()) {
    return {};
  }
  int line = 1;
  std::size_t start = 0;
  for (std::size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\n') {
      if (line == line_1) {
        std::string s = source.substr(start, i - start);
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
          s.pop_back();
        }
        std::size_t a = 0;
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) {
          ++a;
        }
        s = s.substr(a);
        if (s.size() > 120) {
          s = s.substr(0, 117) + "...";
        }
        return s;
      }
      ++line;
      start = i + 1;
    }
  }
  if (line == line_1) {
    std::string s = source.substr(start);
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
      s.pop_back();
    }
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) {
      ++a;
    }
    s = s.substr(a);
    if (s.size() > 120) {
      s = s.substr(0, 117) + "...";
    }
    return s;
  }
  return {};
}

void collect_identifier_refs(TSNode node, const std::string& source,
                             const std::unordered_set<std::string>& def_names_here,
                             const std::string& rel_file, std::vector<RepoMapTag>* out) {
  if (ts_node_is_null(node) || out == nullptr) {
    return;
  }
  const char* type = ts_node_type(node);
  if (is_identifier_node_type(type)) {
    const std::string name = ts_node_text(node, source);
    if (!is_noisy_ident(name) && def_names_here.count(name) == 0) {
      RepoMapTag tag;
      tag.tag_kind = RepoMapTagKind::Ref;
      tag.name = name;
      tag.symbol_kind = SymbolKind::kVariable;
      tag.rel_file = rel_file;
      tag.line = static_cast<int>(ts_node_start_point(node).row) + 1;
      out->push_back(std::move(tag));
    }
  }
  const uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; ++i) {
    collect_identifier_refs(ts_node_child(node, i), source, def_names_here, rel_file, out);
  }
}

TSTree* parse_sync(const std::string& source, const std::string& path) {
  if (source.empty()) {
    return nullptr;
  }
  const TSLanguage* language = tree_sitter_language_for_path(path);
  if (language == nullptr) {
    language = tree_sitter_cpp_language();
  }
  TSParser* parser = ts_parser_new();
  ts_parser_set_language(parser, language);
  TSTree* tree =
      ts_parser_parse_string(parser, nullptr, source.c_str(), static_cast<uint32_t>(source.size()));
  ts_parser_delete(parser);
  return tree;
}

}  // namespace

std::vector<RepoMapTag> extract_repo_map_tags(const std::string& abs_path,
                                             const std::string& rel_path,
                                             const std::string& source) {
  std::vector<RepoMapTag> out;
  if (source.empty()) {
    return out;
  }
  const std::string path_for_lang = abs_path.empty() ? rel_path : abs_path;
  TSTree* tree = parse_sync(source, path_for_lang);
  if (tree == nullptr) {
    return out;
  }
  const TSNode root = ts_tree_root_node(tree);
  if (ts_node_is_null(root)) {
    ts_tree_delete(tree);
    return out;
  }

  const auto symbols = extract_symbols_from_tree(root, source, path_for_lang);
  std::unordered_set<std::string> def_names;
  for (const auto& sym : symbols) {
    const std::string raw = symbol_insert_name(sym.name);
    if (raw.size() < 2) {
      continue;
    }
    RepoMapTag tag;
    tag.tag_kind = RepoMapTagKind::Def;
    tag.name = raw;
    tag.symbol_kind = sym.kind;
    tag.rel_file = rel_path.empty() ? sym.file : rel_path;
    tag.line = sym.line;
    tag.signature = source_line_1based(source, sym.line);
    def_names.insert(raw);
    out.push_back(std::move(tag));
  }

  // Refs: all identifier-like nodes except names defined in this file (reduces self-edges).
  std::vector<RepoMapTag> refs;
  collect_identifier_refs(root, source, def_names, rel_path, &refs);
  // Dedupe identical (name,line) refs; keep first.
  std::unordered_set<std::string> seen_ref;
  for (auto& r : refs) {
    const std::string key = r.name + "#" + std::to_string(r.line);
    if (!seen_ref.insert(key).second) {
      continue;
    }
    out.push_back(std::move(r));
  }

  ts_tree_delete(tree);
  return out;
}

std::vector<RepoMapTag> extract_repo_map_tags_for_file(const std::string& workspace_root,
                                                      const std::string& rel_path) {
  if (rel_path.empty()) {
    return {};
  }
  const std::string abs =
      workspace_root.empty() ? rel_path : (fs::path(workspace_root) / rel_path).string();
  const std::string source = join_editor_lines_from_file(abs);
  return extract_repo_map_tags(abs, rel_path, source);
}

}  // namespace tuide
