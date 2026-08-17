#include "ai/search_replace.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace tuide {
namespace {

void offset_to_line_col(const std::string& text, std::size_t offset, int* line1, int* col1) {
  int line = 1;
  int col = 1;
  for (std::size_t i = 0; i < offset && i < text.size(); ++i) {
    if (text[i] == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  if (line1) {
    *line1 = line;
  }
  if (col1) {
    *col1 = col;
  }
}

std::string read_all(const fs::path& p) {
  std::ifstream in(p);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Collapse CRLF, strip trailing spaces/tabs per line, collapse blank-line runs to one '\n'.
// to_orig[i] = original byte index of normalized[i].
struct FlexNorm {
  std::string text;
  std::vector<std::size_t> to_orig;
};

FlexNorm flex_normalize(const std::string& s) {
  FlexNorm out;
  out.text.reserve(s.size());
  out.to_orig.reserve(s.size());
  auto emit = [&](char c, std::size_t orig_i) {
    out.text.push_back(c);
    out.to_orig.push_back(orig_i);
  };
  auto skip_eol = [&](std::size_t j) -> std::size_t {
    if (j >= s.size()) {
      return j;
    }
    if (s[j] == '\r') {
      ++j;
      if (j < s.size() && s[j] == '\n') {
        ++j;
      }
      return j;
    }
    if (s[j] == '\n') {
      return j + 1;
    }
    return j;
  };
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t line_start = i;
    std::size_t content_end = i;
    bool has_content = false;
    while (i < s.size() && s[i] != '\n' && s[i] != '\r') {
      if (s[i] != ' ' && s[i] != '\t') {
        has_content = true;
        content_end = i + 1;
      }
      ++i;
    }
    const std::size_t eol_at = i;
    if (has_content) {
      for (std::size_t k = line_start; k < content_end; ++k) {
        emit(s[k], k);
      }
      if (eol_at < s.size()) {
        emit('\n', eol_at);
        i = skip_eol(eol_at);
        // Skip further blank lines (optional ws + eol).
        while (i < s.size()) {
          std::size_t t = i;
          while (t < s.size() && (s[t] == ' ' || s[t] == '\t')) {
            ++t;
          }
          if (t < s.size() && (s[t] == '\n' || s[t] == '\r')) {
            i = skip_eol(t);
            continue;
          }
          break;
        }
      }
    } else {
      // Blank line — drop (collapse into prior content newline).
      if (eol_at < s.size()) {
        i = skip_eol(eol_at);
      } else {
        break;
      }
    }
  }
  return out;
}

void fill_span(const std::string& haystack, std::size_t begin, std::size_t end,
               SearchReplaceSpan* out) {
  if (!out) {
    return;
  }
  out->byte_begin = begin;
  out->byte_end = end;
  offset_to_line_col(haystack, out->byte_begin, &out->start_line, &out->start_col);
  offset_to_line_col(haystack, out->byte_end, &out->end_line, &out->end_col);
}

}  // namespace

bool find_unique_span(const std::string& haystack, const std::string& needle, SearchReplaceSpan* out,
                      std::string* err) {
  if (needle.empty()) {
    if (err) {
      *err = "search vacío";
    }
    return false;
  }
  const auto first = haystack.find(needle);
  if (first == std::string::npos) {
    if (err) {
      *err = "search no encontrado (0 matches)";
    }
    return false;
  }
  const auto second = haystack.find(needle, first + needle.size());
  if (second != std::string::npos) {
    if (err) {
      *err = "search ambiguo (≥2 matches)";
    }
    return false;
  }
  fill_span(haystack, first, first + needle.size(), out);
  return true;
}

bool find_unique_span_flex(const std::string& haystack, const std::string& needle,
                           SearchReplaceSpan* out, std::string* err) {
  if (needle.empty()) {
    if (err) {
      *err = "search vacío";
    }
    return false;
  }
  const FlexNorm h = flex_normalize(haystack);
  const FlexNorm n = flex_normalize(needle);
  if (n.text.empty()) {
    if (err) {
      *err = "search vacío";
    }
    return false;
  }
  const auto first = h.text.find(n.text);
  if (first == std::string::npos) {
    if (err) {
      *err = "search no encontrado (0 matches)";
    }
    return false;
  }
  const auto second = h.text.find(n.text, first + n.text.size());
  if (second != std::string::npos) {
    if (err) {
      *err = "search ambiguo (≥2 matches)";
    }
    return false;
  }
  const std::size_t norm_end = first + n.text.size();
  const std::size_t byte_begin = h.to_orig[first];
  const std::size_t byte_end =
      (norm_end < h.to_orig.size()) ? h.to_orig[norm_end] : haystack.size();
  fill_span(haystack, byte_begin, byte_end, out);
  return true;
}

bool find_unique_span_allow_flex(const std::string& haystack, const std::string& needle,
                                 SearchReplaceSpan* out, std::string* err) {
  std::string exact_err;
  if (find_unique_span(haystack, needle, out, &exact_err)) {
    if (err) {
      err->clear();
    }
    return true;
  }
  if (exact_err.find("0 matches") == std::string::npos) {
    if (err) {
      *err = exact_err;
    }
    return false;
  }
  return find_unique_span_flex(haystack, needle, out, err);
}

bool extend_span_to_matching_brace(const std::string& haystack, SearchReplaceSpan* span,
                                   std::string* err) {
  if (!span) {
    if (err) {
      *err = "span nulo";
    }
    return false;
  }
  std::size_t i = span->byte_begin;
  const std::size_t limit = haystack.size();
  while (i < limit && haystack[i] != '{') {
    ++i;
  }
  if (i >= limit) {
    if (err) {
      *err = "sin '{' para cerrar el span";
    }
    return false;
  }
  int depth = 0;
  bool in_str = false;
  bool in_char = false;
  bool escape = false;
  bool in_line_comment = false;
  bool in_block_comment = false;
  std::size_t end = i;
  for (; i < limit; ++i) {
    const char c = haystack[i];
    const char n = (i + 1 < limit) ? haystack[i + 1] : '\0';
    if (in_line_comment) {
      if (c == '\n') {
        in_line_comment = false;
      }
      continue;
    }
    if (in_block_comment) {
      if (c == '*' && n == '/') {
        in_block_comment = false;
        ++i;
      }
      continue;
    }
    if (in_str || in_char) {
      if (escape) {
        escape = false;
        continue;
      }
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (in_str && c == '"') {
        in_str = false;
      } else if (in_char && c == '\'') {
        in_char = false;
      }
      continue;
    }
    if (c == '/' && n == '/') {
      in_line_comment = true;
      ++i;
      continue;
    }
    if (c == '/' && n == '*') {
      in_block_comment = true;
      ++i;
      continue;
    }
    if (c == '"') {
      in_str = true;
      continue;
    }
    if (c == '\'') {
      in_char = true;
      continue;
    }
    if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) {
        end = i + 1;
        // Include trailing newline if present.
        if (end < limit && haystack[end] == '\r') {
          ++end;
        }
        if (end < limit && haystack[end] == '\n') {
          ++end;
        }
        fill_span(haystack, span->byte_begin, end, span);
        return true;
      }
    }
  }
  if (err) {
    *err = "llave '{' sin cierre";
  }
  return false;
}

std::string disk_excerpt_near_search(const std::string& file_text, const std::string& search,
                                     int context_lines, int max_lines) {
  if (file_text.empty() || max_lines <= 0) {
    return {};
  }
  if (context_lines < 0) {
    context_lines = 0;
  }
  std::vector<std::string> lines;
  {
    std::istringstream in(file_text);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      lines.push_back(std::move(line));
    }
  }
  if (lines.empty()) {
    return {};
  }
  int anchor = 0;
  std::string key;
  {
    std::istringstream in(search);
    std::string line;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.pop_back();
      }
      if (!line.empty()) {
        key = line;
        break;
      }
    }
  }
  if (!key.empty()) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
      std::string t = lines[i];
      while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) {
        t.pop_back();
      }
      if (t == key || t.find(key) != std::string::npos) {
        anchor = static_cast<int>(i);
        break;
      }
    }
  }
  const int start = std::max(0, anchor - context_lines);
  const int end = std::min(static_cast<int>(lines.size()), start + max_lines);
  std::ostringstream out;
  for (int i = start; i < end; ++i) {
    out << lines[static_cast<std::size_t>(i)] << '\n';
  }
  return out.str();
}

std::string normalize_hunk_escape_noise(std::string text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      const char n = text[i + 1];
      if (n == 's') {
        out.push_back('\n');
        i += 2;
        if (i < text.size() && text[i] == '*') {
          ++i;
        }
        continue;
      }
      if (n == 'n') {
        out.push_back('\n');
        i += 2;
        continue;
      }
      if (n == 't') {
        out.push_back('\t');
        i += 2;
        continue;
      }
    }
    out.push_back(text[i]);
    ++i;
  }
  return out;
}

void normalize_hunk_escape_noise(SearchReplaceHunk* hunk) {
  if (!hunk) {
    return;
  }
  hunk->search = normalize_hunk_escape_noise(std::move(hunk->search));
  hunk->replace = normalize_hunk_escape_noise(std::move(hunk->replace));
}

std::vector<SearchReplaceHunk> parse_search_replace_json(const nlohmann::json& j, std::string* err) {
  std::vector<SearchReplaceHunk> out;
  nlohmann::json arr;
  if (j.is_array()) {
    arr = j;
  } else if (j.contains("hunks") && j["hunks"].is_array()) {
    arr = j["hunks"];
  } else {
    if (err) {
      *err = "JSON edit sin array hunks";
    }
    return out;
  }
  for (const auto& h : arr) {
    SearchReplaceHunk hunk;
    hunk.path = h.value("path", "");
    hunk.search = h.value("search", "");
    hunk.replace = h.value("replace", "");
    normalize_hunk_escape_noise(&hunk);
    if (hunk.path.empty() || hunk.search.empty()) {
      if (err) {
        *err = "hunk sin path o search";
      }
      return {};
    }
    out.push_back(std::move(hunk));
  }
  if (out.empty() && err) {
    *err = "hunks vacío";
  }
  return out;
}

std::vector<SearchReplaceHunk> parse_search_replace_aider(const std::string& text, std::string* err) {
  std::vector<SearchReplaceHunk> out;
  const std::string search_mark = "<<<<<<< SEARCH";
  const std::string mid_mark = "=======";
  const std::string end_mark = ">>>>>>> REPLACE";
  std::size_t pos = 0;
  std::string current_path;
  while (true) {
    const auto s = text.find(search_mark, pos);
    if (s == std::string::npos) {
      break;
    }
    // Optional path on previous non-empty line.
    std::size_t line_start = s;
    while (line_start > 0 && text[line_start - 1] != '\n') {
      --line_start;
    }
    if (line_start > 0) {
      std::size_t prev_end = line_start - 1;
      std::size_t prev_start = prev_end;
      while (prev_start > 0 && text[prev_start - 1] != '\n') {
        --prev_start;
      }
      std::string prev = text.substr(prev_start, prev_end - prev_start);
      while (!prev.empty() && (prev.back() == '\r' || prev.back() == ' ')) {
        prev.pop_back();
      }
      if (!prev.empty() && prev.find(search_mark) == std::string::npos &&
          prev.find(end_mark) == std::string::npos) {
        current_path = prev;
      }
    }
    const auto s_nl = text.find('\n', s);
    if (s_nl == std::string::npos) {
      if (err) {
        *err = "SEARCH sin cuerpo";
      }
      return {};
    }
    const auto mid = text.find(mid_mark, s_nl + 1);
    if (mid == std::string::npos) {
      if (err) {
        *err = "falta =======";
      }
      return {};
    }
    const auto mid_nl = text.find('\n', mid);
    const auto end = text.find(end_mark, mid);
    if (end == std::string::npos) {
      if (err) {
        *err = "falta >>>>>>> REPLACE";
      }
      return {};
    }
    SearchReplaceHunk hunk;
    hunk.path = current_path;
    hunk.search = text.substr(s_nl + 1, mid - (s_nl + 1));
    if (!hunk.search.empty() && hunk.search.back() == '\n') {
      // keep trailing newline as in file if present before =======
    }
    const std::size_t repl_begin = mid_nl == std::string::npos ? mid + mid_mark.size() : mid_nl + 1;
    hunk.replace = text.substr(repl_begin, end - repl_begin);
    if (hunk.path.empty()) {
      if (err) {
        *err = "bloque Aider sin path";
      }
      return {};
    }
    out.push_back(std::move(hunk));
    pos = end + end_mark.size();
  }
  if (out.empty() && err) {
    *err = "sin bloques SEARCH/REPLACE";
  }
  return out;
}

ApplyHunkResult apply_hunk_to_text(const std::string& text, const SearchReplaceHunk& hunk) {
  ApplyHunkResult r;
  r.before = text;
  r.new_text = hunk.replace;
  std::string err;
  if (!find_unique_span_allow_flex(text, hunk.search, &r.span, &err)) {
    r.old_text = hunk.search;
    r.error = err;
    return r;
  }
  r.old_text = text.substr(r.span.byte_begin, r.span.byte_end - r.span.byte_begin);
  r.after = text.substr(0, r.span.byte_begin) + hunk.replace + text.substr(r.span.byte_end);
  r.ok = true;
  return r;
}

bool write_text_file(const std::string& abs_path, const std::string& body, std::string* err) {
  std::error_code ec;
  fs::create_directories(fs::path(abs_path).parent_path(), ec);
  std::ofstream out(abs_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir " + abs_path;
    }
    return false;
  }
  out << body;
  return true;
}

ApplyHunkResult apply_hunk_to_workspace_file(const std::string& workspace_root,
                                             const SearchReplaceHunk& hunk, bool write) {
  ApplyHunkResult r;
  fs::path abs = hunk.path;
  if (!abs.is_absolute()) {
    abs = fs::path(workspace_root) / hunk.path;
  }
  abs = abs.lexically_normal();
  r.abs_path = abs.string();
  if (!fs::exists(abs)) {
    r.error = "archivo no existe: " + hunk.path;
    return r;
  }
  const std::string text = read_all(abs);
  if (text.empty() && fs::file_size(abs) > 0) {
    r.error = "no se pudo leer " + hunk.path;
    return r;
  }
  r = apply_hunk_to_text(text, hunk);
  r.abs_path = abs.string();
  if (!r.ok) {
    return r;
  }
  if (write) {
    std::string err;
    if (!write_text_file(r.abs_path, r.after, &err)) {
      r.ok = false;
      r.error = err;
      return r;
    }
  }
  return r;
}

}  // namespace tuide
