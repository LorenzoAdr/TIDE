#include "ai/get_code_of.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>

#include "parser/tree_sitter_language.hpp"
#include "parser/tree_sitter_symbols.hpp"
#include "symbols/symbol_utils.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string trim_copy(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return std::string(s);
}

std::string resolve_abs_path(const std::string& root, const std::string& file) {
  if (file.empty()) {
    return {};
  }
  fs::path p(file);
  if (p.is_absolute()) {
    return p.lexically_normal().string();
  }
  if (root.empty()) {
    return p.lexically_normal().string();
  }
  return (fs::path(root) / p).lexically_normal().string();
}

std::string read_file_source(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string read_lines_range(const std::string& path, int start_1, int end_1, int max_lines,
                             bool* truncated) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  std::string line;
  int n = 0;
  int emitted = 0;
  while (std::getline(in, line)) {
    ++n;
    if (n < start_1) {
      continue;
    }
    if (n > end_1) {
      break;
    }
    out << line << '\n';
    if (++emitted >= max_lines) {
      out << "…\n";
      if (truncated != nullptr) {
        *truncated = true;
      }
      break;
    }
  }
  return out.str();
}

int count_source_lines(const std::string& source) {
  if (source.empty()) {
    return 0;
  }
  return static_cast<int>(std::count(source.begin(), source.end(), '\n') +
                          (source.back() == '\n' ? 0 : 1));
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

int kind_priority(SymbolKind k) {
  switch (k) {
    case SymbolKind::kClass:
    case SymbolKind::kStruct:
      return 40;
    case SymbolKind::kFunction:
    case SymbolKind::kMethod:
      return 30;
    case SymbolKind::kNamespace:
      return 10;
    default:
      return 0;
  }
}

bool names_match(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) {
    return false;
  }
  return ascii_lower(a) == ascii_lower(b);
}

const SymbolInfo* pick_symbol(const std::vector<SymbolInfo>& syms, const std::string& want_name,
                              int line_hint) {
  const SymbolInfo* best = nullptr;
  int best_score = -1;
  for (const auto& sym : syms) {
    const std::string bare = symbol_insert_name(sym.name);
    if (!want_name.empty() && !names_match(bare, want_name) && !names_match(sym.name, want_name)) {
      continue;
    }
    int score = kind_priority(sym.kind);
    if (line_hint > 0 && sym.line > 0) {
      const int dist = std::abs(sym.line - line_hint);
      score += std::max(0, 200 - dist);
      if (line_hint >= sym.line && (sym.end_line <= 0 || line_hint <= sym.end_line)) {
        score += 80;
      }
    }
    if (want_name.empty() && kind_priority(sym.kind) < 30) {
      continue;
    }
    if (score > best_score) {
      best_score = score;
      best = &sym;
    }
  }
  if (best != nullptr) {
    return best;
  }
  // Innermost-ish: among scopes containing line_hint, prefer smallest span.
  if (line_hint > 0) {
    const SymbolInfo* inner = nullptr;
    int best_span = 1'000'000;
    for (const auto& sym : syms) {
      if (sym.line <= 0 || kind_priority(sym.kind) < 30) {
        continue;
      }
      const int end = sym.end_line > 0 ? sym.end_line : sym.line;
      if (line_hint < sym.line || line_hint > end) {
        continue;
      }
      const int span = end - sym.line;
      if (span < best_span) {
        best_span = span;
        inner = &sym;
      }
    }
    return inner;
  }
  return nullptr;
}

}  // namespace

GetCodeOfRequest parse_get_code_of_arg(const std::string& arg, const std::string& workspace_root) {
  GetCodeOfRequest req;
  req.workspace_root = workspace_root;
  const std::string trimmed = trim_copy(arg);
  if (trimmed.empty()) {
    return req;
  }

  // Prefer "path:Symbol" / "path:42" when the left side looks like a path.
  const auto colon = trimmed.rfind(':');
  if (colon != std::string::npos && colon > 0 && colon + 1 < trimmed.size()) {
    const std::string left = trimmed.substr(0, colon);
    const std::string right = trimmed.substr(colon + 1);
    const bool left_looks_path =
        left.find('/') != std::string::npos || left.find('\\') != std::string::npos ||
        left.find('.') != std::string::npos;
    if (left_looks_path) {
      req.file = left;
      bool all_digits = !right.empty();
      int line = 0;
      for (char ch : right) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
          all_digits = false;
          break;
        }
        line = line * 10 + (ch - '0');
      }
      if (all_digits && line > 0) {
        req.line = line;
      } else {
        req.symbol = right;
      }
      return req;
    }
  }

  if (trimmed.find('/') != std::string::npos || trimmed.find('.') != std::string::npos) {
    req.file = trimmed;
  } else {
    req.symbol = trimmed;
  }
  return req;
}

GetCodeOfResult get_code_of(const GetCodeOfRequest& req) {
  GetCodeOfResult out;
  const int max_lines = std::max(1, req.max_lines);

  std::string abs = resolve_abs_path(req.workspace_root, req.file);
  if (abs.empty() && !req.symbol.empty() && !req.workspace_root.empty()) {
    // Symbol-only: cannot resolve without an index here; caller should set file.
    out.error = "get_code_of: falta archivo (usa path:Symbol o path:line)";
    return out;
  }
  if (abs.empty()) {
    out.error = "get_code_of: path vacío";
    return out;
  }

  std::error_code ec;
  if (!fs::exists(abs, ec)) {
    out.error = "get_code_of: no existe " + abs;
    return out;
  }

  const std::string source = read_file_source(abs);
  if (source.empty()) {
    out.error = "get_code_of: no se pudo leer " + abs;
    return out;
  }

  out.path = abs;
  const int total_lines = count_source_lines(source);

  TSTree* tree = parse_sync(source, abs);
  std::vector<SymbolInfo> syms;
  if (tree != nullptr) {
    const TSNode root = ts_tree_root_node(tree);
    if (!ts_node_is_null(root)) {
      syms = extract_symbols_from_tree(root, source, abs);
    }
    ts_tree_delete(tree);
  }

  const SymbolInfo* picked = pick_symbol(syms, req.symbol, req.line);
  if (picked != nullptr) {
    out.name = symbol_insert_name(picked->name);
    out.kind = picked->kind;
    out.start_line = std::max(1, picked->line);
    out.end_line = picked->end_line > 0 ? picked->end_line : out.start_line;
  } else if (req.line > 0) {
    // Fallback: window around the line.
    out.name = req.symbol.empty() ? fs::path(abs).stem().string() : req.symbol;
    out.start_line = std::max(1, req.line - 2);
    out.end_line = std::min(total_lines, req.line + max_lines - 1);
  } else {
    // File-level: start of file (scripts / no matching symbol).
    out.name = req.symbol.empty() ? fs::path(abs).stem().string() : req.symbol;
    out.start_line = 1;
    out.end_line = std::min(total_lines, max_lines);
  }

  if (out.end_line < out.start_line) {
    out.end_line = out.start_line;
  }
  const int span = out.end_line - out.start_line + 1;
  if (span > max_lines) {
    out.end_line = out.start_line + max_lines - 1;
    out.truncated = true;
  }

  bool trunc = out.truncated;
  out.text = read_lines_range(abs, out.start_line, out.end_line, max_lines, &trunc);
  out.truncated = trunc || out.truncated;
  if (out.text.empty()) {
    out.error = "get_code_of: cuerpo vacío";
    return out;
  }
  out.ok = true;
  return out;
}

namespace {

std::string fence_lang_for_path(const std::string& path) {
  const auto dot = path.find_last_of('.');
  if (dot == std::string::npos) {
    return {};
  }
  const std::string ext = path.substr(dot + 1);
  if (ext == "hpp" || ext == "h" || ext == "hh" || ext == "cpp" || ext == "cc" || ext == "cxx" ||
      ext == "c") {
    return "cpp";
  }
  if (ext == "py") {
    return "python";
  }
  if (ext == "rs") {
    return "rust";
  }
  if (ext == "go") {
    return "go";
  }
  if (ext == "sh" || ext == "bash") {
    return "bash";
  }
  if (ext == "js" || ext == "mjs" || ext == "cjs") {
    return "javascript";
  }
  if (ext == "ts" || ext == "tsx") {
    return "typescript";
  }
  if (ext == "cmake" || path.find("CMakeLists") != std::string::npos) {
    return "cmake";
  }
  return {};
}

}  // namespace

std::string dump_context_last_md(const std::string& workspace_root, const std::string& query,
                                 const RepoMap& map, std::size_t max_n, std::string* err_out,
                                 const SymbolIndexSnapshot* snapshot, EmbeddingBackend* embed,
                                 CodingStemEmbedIndex* stem_index) {
  if (workspace_root.empty()) {
    if (err_out != nullptr) {
      *err_out = "sin workspace_root";
    }
    return {};
  }
  const fs::path dir = fs::path(workspace_root) / ".tuide" / "ai";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    if (err_out != nullptr) {
      *err_out = "no se pudo crear " + dir.string();
    }
    return {};
  }
  const fs::path out_path = dir / "context_last.md";
  std::ofstream out(out_path, std::ios::trunc);
  if (!out) {
    if (err_out != nullptr) {
      *err_out = "no se pudo escribir " + out_path.string();
    }
    return {};
  }

  RepoMap work = map;
  work.coding_embed = embed;
  work.coding_stem_index = stem_index;
  work.enrich_dominant_stem_from_snapshot(snapshot, query, 48);

  const auto outline = work.coding_outline_entries(24, query);
  const auto bodies = work.ranked_coding_entries(max_n == 0 ? 14 : max_n, query);

  std::string stem_label = !work.context_stem.empty() ? work.context_stem : "?";
  if (stem_label == "?") {
    if (!outline.empty()) {
      const auto slash = outline.front().file.find_last_of('/');
      std::string base = slash == std::string::npos ? outline.front().file
                                                    : outline.front().file.substr(slash + 1);
      const auto dot = base.find_last_of('.');
      if (dot != std::string::npos) {
        base = base.substr(0, dot);
      }
      stem_label = base;
    } else if (!bodies.empty()) {
      const auto slash = bodies.front().file.find_last_of('/');
      std::string base =
          slash == std::string::npos ? bodies.front().file : bodies.front().file.substr(slash + 1);
      const auto dot = base.find_last_of('.');
      if (dot != std::string::npos) {
        base = base.substr(0, dot);
      }
      stem_label = base;
    }
  }

  // Avoid "query: query: …" when the caller already prefixed the string.
  std::string query_line = query;
  while (true) {
    auto begin = query_line.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
      query_line.clear();
      break;
    }
    if (begin > 0) {
      query_line.erase(0, begin);
    }
    if (query_line.size() >= 6) {
      const std::string head = query_line.substr(0, 6);
      std::string head_l = head;
      for (char& c : head_l) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (head_l == "query:") {
        query_line.erase(0, 6);
        continue;
      }
    }
    break;
  }
  {
    auto begin = query_line.find_first_not_of(" \t");
    if (begin != std::string::npos && begin > 0) {
      query_line.erase(0, begin);
    }
  }

  out << "# Context dump (coding pack)\n\n";
  out << "query: " << query_line << "\n\n";
  if (!work.note.empty()) {
    work.note += "; ";
  }
  work.note += "coding_pack=1; outline=" + std::to_string(outline.size()) +
               "; bodies=" + std::to_string(bodies.size());
  out << "note: " << work.note << "\n\n";

  out << "## Outline (stem=" << stem_label << ")\n\n";
  if (outline.empty()) {
    out << "(vacío)\n\n";
  } else {
    for (std::size_t i = 0; i < outline.size(); ++i) {
      const auto& e = outline[i];
      out << (i + 1) << ". " << e.file;
      if (e.line > 0) {
        out << ':' << e.line;
      }
      if (!e.name.empty()) {
        out << " — `" << e.name << "`";
      }
      if (!e.signature.empty()) {
        out << " — `" << e.signature << "`";
      }
      out << '\n';
    }
    out << '\n';
  }

  out << "## Bodies\n\n";
  int n = 0;
  int ok_bodies = 0;
  for (const auto& e : bodies) {
    ++n;
    out << "### " << n << ". " << e.file;
    if (e.line > 0) {
      out << ':' << e.line;
    }
    if (!e.name.empty()) {
      out << " — `" << e.name << "`";
    }
    out << "\n\n";
    if (!e.signature.empty()) {
      out << "firma: `" << e.signature << "`\n\n";
    }

    // Skip dumping the generic start-of-file when we already have real symbols.
    if (e.signature.rfind("file ", 0) == 0 && n > 1) {
      out << "_file anchor — ver Outline_\n\n";
      continue;
    }

    GetCodeOfRequest req;
    req.workspace_root = workspace_root;
    req.file = e.file;
    req.symbol = e.name;
    req.line = e.line;
    const bool fn =
        e.kind == SymbolKind::kFunction || e.kind == SymbolKind::kMethod ||
        (!e.signature.empty() && e.signature.find('(') != std::string::npos);
    req.max_lines = fn ? 200 : 120;
    const GetCodeOfResult got = get_code_of(req);
    if (!got.ok) {
      out << "_sin cuerpo: " << (got.error.empty() ? "error" : got.error) << "_\n\n";
      continue;
    }
    ++ok_bodies;
    out << "```" << fence_lang_for_path(got.path) << '\n';
    out << got.text;
    if (!got.text.empty() && got.text.back() != '\n') {
      out << '\n';
    }
    out << "```\n\n";
  }
  if (n == 0) {
    out << "(sin entradas)\n";
  }
  out << "<!-- bodies_ok=" << ok_bodies << " entries=" << n << " outline=" << outline.size()
      << " coding_pack=1 -->\n";
  return out_path.lexically_normal().string();
}

std::string dump_ranked_map_md(const RankedMapDumpOptions& opts, std::string* err_out) {
  if (opts.workspace_root.empty()) {
    if (err_out != nullptr) {
      *err_out = "sin workspace_root";
    }
    return {};
  }
  const fs::path dir = fs::path(opts.workspace_root) / ".tuide" / "ai";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    if (err_out != nullptr) {
      *err_out = "no se pudo crear " + dir.string();
    }
    return {};
  }

  std::string query_line = opts.query;
  while (true) {
    auto begin = query_line.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
      query_line.clear();
      break;
    }
    if (begin > 0) {
      query_line.erase(0, begin);
    }
    if (query_line.size() >= 6) {
      const std::string head = query_line.substr(0, 6);
      std::string head_l = head;
      for (char& c : head_l) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (head_l == "query:") {
        query_line.erase(0, 6);
        continue;
      }
    }
    break;
  }
  {
    auto begin = query_line.find_first_not_of(" \t");
    if (begin != std::string::npos && begin > 0) {
      query_line.erase(0, begin);
    }
  }

  const std::string fname = opts.filename.empty() ? std::string("map_last.md") : opts.filename;
  const fs::path out_path = dir / fname;
  std::ostringstream md;
  md << "# Ranked map (L1 → L2)\n\n";
  md << "query: " << query_line << "\n\n";
  std::string note = opts.note;
  if (!note.empty()) {
    note += "; ";
  }
  note += "ranked_map=1; entries=" + std::to_string(opts.entries.size());
  if (opts.include_bodies) {
    note += "; bodies=1";
  }
  md << "note: " << note << "\n\n";

  md << "## Ranked entries\n\n";
  const std::size_t max_e = opts.max_entries == 0 ? opts.entries.size() : opts.max_entries;
  std::size_t shown = 0;
  for (const auto& e : opts.entries) {
    if (shown >= max_e) {
      break;
    }
    ++shown;
    md << shown << ". " << e.file;
    if (e.line > 0) {
      md << ':' << e.line;
    }
    md << "  [score=" << e.score << "]";
    if (!e.name.empty()) {
      md << " — `" << e.name << "`";
    }
    md << '\n';
    if (!e.signature.empty()) {
      md << "    `" << e.signature << "`\n";
    }
    const std::string why = format_entry_hints_line(e);
    if (!why.empty()) {
      md << "    " << why << '\n';
    }
    if (!e.doc_line.empty()) {
      md << "    doc: " << e.doc_line << '\n';
    }
    if (!e.snippet.empty()) {
      md << "    ```\n";
      std::istringstream sn(e.snippet);
      std::string sl;
      while (std::getline(sn, sl)) {
        md << "    " << sl << '\n';
      }
      md << "    ```\n";
    }
  }
  if (shown == 0) {
    md << "(vacío)\n";
  }
  md << '\n';

  int ok_bodies = 0;
  int n_bodies = 0;
  if (opts.include_bodies) {
    md << "## Bodies\n\n";
    const std::size_t max_b = opts.max_bodies == 0 ? opts.entries.size() : opts.max_bodies;
    for (std::size_t i = 0; i < opts.entries.size() && static_cast<std::size_t>(n_bodies) < max_b;
         ++i) {
      const auto& e = opts.entries[i];
      ++n_bodies;
      md << "### " << n_bodies << ". " << e.file;
      if (e.line > 0) {
        md << ':' << e.line;
      }
      if (!e.name.empty()) {
        md << " — `" << e.name << "`";
      }
      md << "\n\n";

      std::string body;
      if (i < opts.body_texts.size()) {
        body = opts.body_texts[i];
      }
      if (body.empty()) {
        GetCodeOfRequest req;
        req.workspace_root = opts.workspace_root;
        req.file = e.file;
        req.symbol = e.name;
        req.line = e.line;
        req.max_lines = 120;
        const GetCodeOfResult got = get_code_of(req);
        if (!got.ok) {
          md << "_sin cuerpo: " << (got.error.empty() ? "error" : got.error) << "_\n\n";
          continue;
        }
        body = got.text;
      }
      ++ok_bodies;
      md << "```" << fence_lang_for_path(e.file) << '\n';
      md << body;
      if (!body.empty() && body.back() != '\n') {
        md << '\n';
      }
      md << "```\n\n";
    }
    if (n_bodies == 0) {
      md << "(sin entradas)\n";
    }
  }

  md << "<!-- ranked_map=1 entries=" << shown << " bodies_ok=" << ok_bodies << " -->\n";
  const std::string content = md.str();

  {
    std::ofstream out(out_path, std::ios::trunc);
    if (!out) {
      if (err_out != nullptr) {
        *err_out = "no se pudo escribir " + out_path.string();
      }
      return {};
    }
    out << content;
  }
  // Mirror for UX continuity with older paths.
  if (fname != "context_last.md") {
    std::ofstream mirror(dir / "context_last.md", std::ios::trunc);
    if (mirror) {
      mirror << content;
    }
  }
  return out_path.lexically_normal().string();
}

}  // namespace tuide
