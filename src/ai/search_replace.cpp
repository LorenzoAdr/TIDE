#include "ai/search_replace.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

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
  if (out) {
    out->byte_begin = first;
    out->byte_end = first + needle.size();
    offset_to_line_col(haystack, out->byte_begin, &out->start_line, &out->start_col);
    offset_to_line_col(haystack, out->byte_end, &out->end_line, &out->end_col);
  }
  return true;
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
  r.old_text = hunk.search;
  r.new_text = hunk.replace;
  std::string err;
  if (!find_unique_span(text, hunk.search, &r.span, &err)) {
    r.error = err;
    return r;
  }
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
