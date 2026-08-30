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

int symbol_span_lines(const SymbolInfo& sym) {
  const int start = std::max(1, sym.line);
  const int end = sym.end_line > 0 ? sym.end_line : start;
  return std::max(0, end - start);
}

bool symbol_span_has_open_brace(const std::string& source, const SymbolInfo& sym) {
  if (source.empty() || sym.line <= 0) {
    return false;
  }
  const int start = std::max(1, sym.line);
  const int end = sym.end_line > 0 ? sym.end_line : start;
  int n = 1;
  std::size_t i = 0;
  while (i < source.size() && n < start) {
    if (source[i] == '\n') {
      ++n;
    }
    ++i;
  }
  while (i < source.size() && n <= end) {
    if (source[i] == '{') {
      return true;
    }
    if (source[i] == '\n') {
      ++n;
    }
    ++i;
  }
  return false;
}

const SymbolInfo* pick_symbol(const std::vector<SymbolInfo>& syms, const std::string& want_name,
                              int line_hint, const std::string& source) {
  const SymbolInfo* best = nullptr;
  int best_score = -1;
  for (const auto& sym : syms) {
    const std::string bare = symbol_insert_name(sym.name);
    if (!want_name.empty() && !names_match(bare, want_name) && !names_match(sym.name, want_name)) {
      continue;
    }
    int score = kind_priority(sym.kind);
    // Prefer a definition (body) over a same-name forward declaration.
    score += std::min(40, symbol_span_lines(sym));
    if (symbol_span_has_open_brace(source, sym)) {
      score += 60;
    }
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

bool parse_positive_int(const std::string& s, int* out) {
  if (s.empty() || out == nullptr) {
    return false;
  }
  int v = 0;
  for (char ch : s) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
    v = v * 10 + (ch - '0');
  }
  if (v <= 0) {
    return false;
  }
  *out = v;
  return true;
}

bool parse_line_range_token(const std::string& s, int* a, int* b) {
  const auto dash = s.find('-');
  if (dash == std::string::npos || dash == 0 || dash + 1 >= s.size()) {
    return false;
  }
  return parse_positive_int(s.substr(0, dash), a) && parse_positive_int(s.substr(dash + 1), b) &&
         *a <= *b;
}

bool path_looks_like_file(const std::string& left) {
  return left.find('/') != std::string::npos || left.find('\\') != std::string::npos ||
         left.find('.') != std::string::npos;
}

std::string relative_display_path(const std::string& abs_or_rel, const std::string& workspace_root) {
  if (workspace_root.empty() || abs_or_rel.empty()) {
    return abs_or_rel;
  }
  std::error_code ec;
  const fs::path abs = fs::weakly_canonical(abs_or_rel, ec);
  const fs::path root = fs::weakly_canonical(workspace_root, ec);
  if (ec || abs.empty() || root.empty()) {
    return abs_or_rel;
  }
  const std::string a = abs.string();
  const std::string r = root.string();
  if (a.size() > r.size() && a.compare(0, r.size(), r) == 0 &&
      (a[r.size()] == '/' || a[r.size()] == '\\')) {
    return a.substr(r.size() + 1);
  }
  return abs_or_rel;
}

// Read [start_1, end_1] without an extra max_lines cap (caller clamps).
std::string read_lines_inclusive(const std::string& path, int start_1, int end_1) {
  bool dummy = false;
  const int span = std::max(1, end_1 - start_1 + 1);
  return read_lines_range(path, start_1, end_1, span + 8, &dummy);
}

}  // namespace

GetCodeOfRequest parse_get_code_of_arg(const std::string& arg, const std::string& workspace_root) {
  GetCodeOfRequest req;
  req.workspace_root = workspace_root;
  std::string trimmed = trim_copy(arg);
  if (trimmed.empty()) {
    return req;
  }

  // Optional window suffix: #head | #mid | #tail
  {
    const auto hash = trimmed.rfind('#');
    if (hash != std::string::npos && hash + 1 < trimmed.size()) {
      std::string w = ascii_lower(trimmed.substr(hash + 1));
      while (!w.empty() && std::isspace(static_cast<unsigned char>(w.back()))) {
        w.pop_back();
      }
      if (w == "head") {
        req.window = GetCodeOfWindow::Head;
        trimmed = trim_copy(trimmed.substr(0, hash));
      } else if (w == "mid") {
        req.window = GetCodeOfWindow::Mid;
        trimmed = trim_copy(trimmed.substr(0, hash));
      } else if (w == "tail") {
        req.window = GetCodeOfWindow::Tail;
        trimmed = trim_copy(trimmed.substr(0, hash));
      }
    }
  }
  if (trimmed.empty()) {
    return req;
  }

  // Prefer "path:Symbol" / "path:42" / "path:10-50" when the left side looks like a path.
  const auto colon = trimmed.rfind(':');
  if (colon != std::string::npos && colon > 0 && colon + 1 < trimmed.size()) {
    const std::string left = trimmed.substr(0, colon);
    const std::string right = trimmed.substr(colon + 1);
    if (path_looks_like_file(left)) {
      req.file = left;
      int a = 0;
      int b = 0;
      if (parse_line_range_token(right, &a, &b)) {
        req.window = GetCodeOfWindow::Range;
        req.range_start = a;
        req.range_end = b;
        req.line = a;
      } else if (parse_positive_int(right, &a)) {
        req.line = a;
      } else {
        req.symbol = right;
      }
      return req;
    }
  }

  if (path_looks_like_file(trimmed)) {
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

  const SymbolInfo* picked = pick_symbol(syms, req.symbol, req.line, source);
  if (picked != nullptr) {
    out.name = symbol_insert_name(picked->name);
    out.kind = picked->kind;
    out.symbol_start = std::max(1, picked->line);
    out.symbol_end = picked->end_line > 0 ? picked->end_line : out.symbol_start;
  } else if (req.window == GetCodeOfWindow::Range && req.range_start > 0) {
    out.name = req.symbol.empty() ? fs::path(abs).stem().string() : req.symbol;
    out.symbol_start = std::max(1, req.range_start);
    out.symbol_end = std::min(total_lines, std::max(req.range_end, req.range_start));
  } else if (req.line > 0) {
    out.name = req.symbol.empty() ? fs::path(abs).stem().string() : req.symbol;
    out.symbol_start = std::max(1, req.line - 2);
    out.symbol_end = std::min(total_lines, req.line + max_lines - 1);
  } else {
    out.name = req.symbol.empty() ? fs::path(abs).stem().string() : req.symbol;
    out.symbol_start = 1;
    out.symbol_end = std::min(total_lines, std::max(1, total_lines));
  }

  if (out.symbol_end < out.symbol_start) {
    out.symbol_end = out.symbol_start;
  }
  out.symbol_start = std::max(1, std::min(out.symbol_start, total_lines));
  out.symbol_end = std::max(out.symbol_start, std::min(out.symbol_end, total_lines));

  const int sym_span = out.symbol_end - out.symbol_start + 1;
  const std::string disp = relative_display_path(abs, req.workspace_root);
  const std::string hint_base =
      !out.name.empty() && picked != nullptr ? (disp + ":" + out.name) : disp;

  auto set_contiguous = [&](int a, int b, bool trunc) {
    out.sent_start = a;
    out.sent_end = b;
    out.start_line = a;
    out.end_line = b;
    out.truncated = trunc;
    out.omitted_start = 0;
    out.omitted_end = 0;
    out.text = read_lines_inclusive(abs, a, b);
  };

  if (req.window == GetCodeOfWindow::Range && req.range_start > 0) {
    int a = std::max(1, req.range_start);
    int b = std::max(a, req.range_end);
    a = std::min(a, total_lines);
    b = std::min(b, total_lines);
    const int span = b - a + 1;
    if (span > max_lines) {
      b = a + max_lines - 1;
      set_contiguous(a, b, true);
      out.refetch_hint = disp + ":" + std::to_string(b + 1) + "-" +
                         std::to_string(std::min(total_lines, b + max_lines));
    } else {
      set_contiguous(a, b, false);
    }
  } else if (sym_span <= max_lines) {
    set_contiguous(out.symbol_start, out.symbol_end, false);
  } else if (req.window == GetCodeOfWindow::Auto && req.line > 0 &&
             req.line >= out.symbol_start && req.line <= out.symbol_end &&
             sym_span > max_lines) {
    // Line hint inside a huge symbol: window around the line (not whole-method head+tail).
    // Bias downward: edit loci (switch/body) usually sit after the matched line.
    const int before = std::max(1, max_lines / 3);
    const int half = max_lines / 2;  // used in refetch note
    int a = std::max(out.symbol_start, req.line - before);
    int b = std::min(out.symbol_end, a + max_lines - 1);
    if (b - a + 1 < max_lines) {
      a = std::max(out.symbol_start, b - max_lines + 1);
    }
    set_contiguous(a, b, true);
    // Prefer adjacent window around the same line (not the symbol head).
    if (a > out.symbol_start) {
      const int prev_b = a - 1;
      const int prev_a = std::max(out.symbol_start, prev_b - max_lines + 1);
      out.refetch_hint = disp + ":" + std::to_string(prev_a) + "-" + std::to_string(prev_b);
    } else if (b < out.symbol_end) {
      const int next_a = b + 1;
      const int next_b = std::min(out.symbol_end, next_a + max_lines - 1);
      out.refetch_hint = disp + ":" + std::to_string(next_a) + "-" + std::to_string(next_b);
    } else {
      out.refetch_hint = disp + ":" + std::to_string(a) + "-" + std::to_string(b);
    }
    if (a > out.symbol_start || b < out.symbol_end) {
      std::ostringstream note;
      note << "… [line-window inside " << (picked ? out.name : std::string("symbol"))
           << " " << out.symbol_start << "-" << out.symbol_end << "; sent " << a << "-" << b
           << "; refetch " << out.refetch_hint << " or " << disp << ":"
           << std::max(out.symbol_start, req.line - half) << "-"
           << std::min(out.symbol_end, req.line + half) << " or " << hint_base
           << "#head|#tail] …\n";
      out.text = note.str() + out.text;
      // Prefer recording the larger omitted side for missing_lines metadata.
      if (a > out.symbol_start && (out.symbol_end - b) <= (a - out.symbol_start)) {
        out.omitted_start = out.symbol_start;
        out.omitted_end = a - 1;
      } else if (b < out.symbol_end) {
        out.omitted_start = b + 1;
        out.omitted_end = out.symbol_end;
      } else if (a > out.symbol_start) {
        out.omitted_start = out.symbol_start;
        out.omitted_end = a - 1;
      }
    }
  } else if (req.window == GetCodeOfWindow::Head) {
    set_contiguous(out.symbol_start, out.symbol_start + max_lines - 1, true);
    out.refetch_hint = hint_base + "#tail";
  } else if (req.window == GetCodeOfWindow::Tail) {
    set_contiguous(out.symbol_end - max_lines + 1, out.symbol_end, true);
    out.refetch_hint = hint_base + "#head";
  } else if (req.window == GetCodeOfWindow::Mid) {
    const int mid = out.symbol_start + (sym_span - 1) / 2;
    int a = mid - max_lines / 2;
    int b = a + max_lines - 1;
    if (a < out.symbol_start) {
      a = out.symbol_start;
      b = a + max_lines - 1;
    }
    if (b > out.symbol_end) {
      b = out.symbol_end;
      a = std::max(out.symbol_start, b - max_lines + 1);
    }
    set_contiguous(a, b, true);
    out.refetch_hint = disp + ":" + std::to_string(out.symbol_start) + "-" +
                       std::to_string(out.symbol_start + max_lines - 1);
  } else {
    // Auto: head + tail (signature + closing region).
    int head_n = std::max(1, (max_lines * 55) / 100);
    int tail_n = std::max(1, max_lines - head_n);
    if (head_n + tail_n >= sym_span) {
      set_contiguous(out.symbol_start, out.symbol_end, false);
    } else {
      const int head_end = out.symbol_start + head_n - 1;
      const int tail_start = out.symbol_end - tail_n + 1;
      const int miss_lo = head_end + 1;
      const int miss_hi = tail_start - 1;
      std::ostringstream body;
      body << read_lines_inclusive(abs, out.symbol_start, head_end);
      body << "… [omitted lines " << miss_lo << "-" << miss_hi << "; refetch "
           << disp << ":" << miss_lo << "-" << std::min(miss_hi, miss_lo + max_lines - 1)
           << " or " << hint_base << "#mid] …\n";
      body << read_lines_inclusive(abs, tail_start, out.symbol_end);
      out.text = body.str();
      out.truncated = true;
      out.omitted_start = miss_lo;
      out.omitted_end = miss_hi;
      out.sent_start = out.symbol_start;
      out.sent_end = out.symbol_end;
      out.start_line = out.symbol_start;
      out.end_line = out.symbol_end;
      out.refetch_hint = disp + ":" + std::to_string(miss_lo) + "-" +
                         std::to_string(std::min(miss_hi, miss_lo + max_lines - 1));
    }
  }

  if (out.text.empty()) {
    out.error = "get_code_of: cuerpo vacío";
    return out;
  }
  out.ok = true;
  return out;
}

std::string format_get_code_of_header(const GetCodeOfResult& got, const std::string& display_path) {
  const std::string path = !display_path.empty() ? display_path : got.path;
  std::ostringstream out;
  // Always show the span actually sent when available (line-windows are not full symbols).
  if (got.sent_start > 0 && got.sent_end >= got.sent_start) {
    out << path << ':' << got.sent_start << '-' << got.sent_end;
  } else if (got.truncated && got.symbol_start > 0) {
    out << path << ':' << got.symbol_start << '-' << got.symbol_end;
  } else {
    out << path << ':' << got.sent_start << '-' << got.sent_end;
  }
  if (!got.name.empty()) {
    out << " (" << got.name << ")";
  }
  if (got.truncated) {
    out << " [TRUNCATED]";
  }
  out << '\n';
  if (got.symbol_start > 0 && got.symbol_end >= got.symbol_start) {
    out << "symbol_span: " << got.symbol_start << '-' << got.symbol_end << '\n';
  }
  if (got.truncated && got.sent_start > 0 && got.sent_end >= got.sent_start) {
    out << "sent: " << got.sent_start << '-' << got.sent_end << '\n';
    if (got.omitted_start > 0 && got.omitted_end >= got.omitted_start) {
      out << "missing_lines: " << got.omitted_start << '-' << got.omitted_end << '\n';
    } else if (got.symbol_end > got.sent_end) {
      out << "missing_lines: " << (got.sent_end + 1) << '-' << got.symbol_end << '\n';
    } else if (got.sent_start > got.symbol_start && got.symbol_start > 0) {
      out << "missing_lines: " << got.symbol_start << '-' << (got.sent_start - 1) << '\n';
    }
  } else if (got.truncated && got.omitted_start > 0 && got.omitted_end >= got.omitted_start) {
    out << "sent: " << got.symbol_start << '-' << (got.omitted_start - 1) << " + "
        << (got.omitted_end + 1) << '-' << got.symbol_end << '\n';
    out << "missing_lines: " << got.omitted_start << '-' << got.omitted_end << '\n';
  } else if (got.truncated) {
    out << "sent: " << got.sent_start << '-' << got.sent_end << '\n';
  }
  if (got.truncated) {
    const std::string hint =
        !got.refetch_hint.empty() ? got.refetch_hint : (path + ":" + std::to_string(got.sent_end + 1));
    out << "refetch: get_code_of `" << hint << "` (o `#head`/`#mid`/`#tail` / `path:A-B`)\n";
    out << "note: cuerpo incompleto — usa refetch; no inventes el código omitido.\n";
  }
  return out.str();
}

std::string format_get_code_of_result(const GetCodeOfResult& got, const std::string& display_path) {
  return format_get_code_of_header(got, display_path) + got.text;
}

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
