#include "editor/doc_comment.hpp"

#include "lsp/lsp_uri.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace tgdb {
namespace {

constexpr int kBannerWidth = 79;

std::string trim_copy(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

std::string indent_of(int cols) {
  if (cols <= 0) {
    return {};
  }
  return std::string(static_cast<std::size_t>(cols), ' ');
}

std::string today_iso_date() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1,
                local.tm_mday);
  return buf;
}

std::string filename_of(const std::string& path) {
  if (path.empty()) {
    return "file";
  }
  return fs::path(path).filename().string();
}

enum class DocStyle {
  kDoxygenBlock,   // /** */ C/C++/JS/TS/Java/Go/Zig
  kPythonDocstring,
  kHashBanner,     // # lines (shell, etc.)
  kTripleSlash,    // /// Rust-style
  kFortranBang,    // !>
  kPlainBlock,     // /* */ generic
};

DocStyle style_for_language(const std::string& language_id) {
  if (language_id == "python") {
    return DocStyle::kPythonDocstring;
  }
  if (language_id == "shellscript") {
    return DocStyle::kHashBanner;
  }
  if (language_id == "rust") {
    return DocStyle::kTripleSlash;
  }
  if (language_id == "fortran") {
    return DocStyle::kFortranBang;
  }
  if (language_id == "c" || language_id == "cpp" || language_id == "go" || language_id == "zig" ||
      language_id == "javascript" || language_id == "typescript") {
    return DocStyle::kDoxygenBlock;
  }
  if (language_id == "lua") {
    return DocStyle::kHashBanner;
  }
  return DocStyle::kDoxygenBlock;
}

DocStyle style_for_path(const std::string& path) {
  return style_for_language(language_id_for_path(path));
}

bool is_callable_kind(SymbolKind kind) {
  return kind == SymbolKind::kFunction || kind == SymbolKind::kMethod;
}

bool is_type_kind(SymbolKind kind) {
  return kind == SymbolKind::kClass || kind == SymbolKind::kStruct ||
         kind == SymbolKind::kNamespace;
}

std::size_t find_matching_close_paren(const std::string& text, std::size_t open_paren) {
  int depth = 0;
  for (std::size_t i = open_paren; i < text.size(); ++i) {
    if (text[i] == '(') {
      ++depth;
    } else if (text[i] == ')') {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::string::npos;
}

std::vector<std::string> split_params(const std::string& args) {
  std::vector<std::string> params;
  std::string current;
  int depth = 0;
  for (char c : args) {
    if (c == '(' || c == '<' || c == '[' || c == '{') {
      ++depth;
      current.push_back(c);
    } else if (c == ')' || c == '>' || c == ']' || c == '}') {
      --depth;
      current.push_back(c);
    } else if (c == ',' && depth == 0) {
      const std::string trimmed = trim_copy(current);
      if (!trimmed.empty()) {
        params.push_back(trimmed);
      }
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  const std::string trimmed = trim_copy(current);
  if (!trimmed.empty()) {
    params.push_back(trimmed);
  }
  return params;
}

std::string strip_default_argument(std::string param) {
  int depth = 0;
  for (std::size_t i = 0; i < param.size(); ++i) {
    const char c = param[i];
    if (c == '(' || c == '<' || c == '[' || c == '{') {
      ++depth;
    } else if (c == ')' || c == '>' || c == ']' || c == '}') {
      --depth;
    } else if ((c == '=' || c == ':') && depth == 0) {
      // Keep type annotation colon for Python only handled elsewhere; for C++ '=' is default.
      if (c == '=') {
        return trim_copy(param.substr(0, i));
      }
    }
  }
  return trim_copy(param);
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string last_identifier(const std::string& text) {
  if (text.empty()) {
    return {};
  }
  int end = static_cast<int>(text.size()) - 1;
  while (end >= 0 && std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(end)]))) {
    --end;
  }
  if (end < 0) {
    return {};
  }
  // Skip trailing refs/pointers: *& 
  while (end >= 0) {
    const char c = text[static_cast<std::size_t>(end)];
    if (c == '*' || c == '&' || std::isspace(static_cast<unsigned char>(c))) {
      --end;
      continue;
    }
    break;
  }
  if (end < 0 || !is_ident_char(text[static_cast<std::size_t>(end)])) {
    return {};
  }
  int start = end;
  while (start >= 0 && is_ident_char(text[static_cast<std::size_t>(start)])) {
    --start;
  }
  return text.substr(static_cast<std::size_t>(start + 1),
                     static_cast<std::size_t>(end - start));
}

std::string python_param_name(std::string param) {
  param = trim_copy(param);
  // *args / **kwargs
  while (!param.empty() && (param.front() == '*' || param.front() == ' ')) {
    param.erase(param.begin());
  }
  // Strip type annotation and default: name: Type = value
  std::size_t cut = param.size();
  int depth = 0;
  for (std::size_t i = 0; i < param.size(); ++i) {
    const char c = param[i];
    if (c == '(' || c == '[' || c == '{') {
      ++depth;
    } else if (c == ')' || c == ']' || c == '}') {
      --depth;
    } else if (depth == 0 && (c == ':' || c == '=')) {
      cut = i;
      break;
    }
  }
  return trim_copy(param.substr(0, cut));
}

bool skip_python_param(const std::string& name) {
  return name.empty() || name == "self" || name == "cls" || name == "/";
}

std::string snippet_ph(int index, const std::string& label) {
  return "${" + std::to_string(index) + ":" + label + "}";
}

std::string pad_banner_line(const std::string& prefix, const std::string& content,
                            const std::string& suffix, int width) {
  // prefix + content + spaces + suffix, total length == width when possible
  std::string line = prefix + content;
  const int content_end = static_cast<int>(line.size());
  const int suffix_len = static_cast<int>(suffix.size());
  int spaces = width - content_end - suffix_len;
  if (spaces < 1) {
    spaces = 1;
  }
  line.append(static_cast<std::size_t>(spaces), ' ');
  line += suffix;
  return line;
}

std::string star_rule(const std::string& open, const std::string& close, int width) {
  // open + ****... + close filling width
  const int inner = width - static_cast<int>(open.size()) - static_cast<int>(close.size());
  std::string line = open;
  if (inner > 0) {
    line.append(static_cast<std::size_t>(inner), '*');
  }
  line += close;
  return line;
}

std::string empty_star_row(const std::string& left, const std::string& right, int width) {
  return pad_banner_line(left, "", right, width);
}

std::string labeled_star_row(const std::string& left, const std::string& label,
                             const std::string& value, const std::string& right, int width) {
  return pad_banner_line(left, label + value, right, width);
}

void append_line(std::ostringstream& out, const std::string& indent, const std::string& line) {
  out << indent << line << '\n';
}

std::string build_doxygen_doc(const DocCommentRequest& request,
                              const std::vector<std::string>& params) {
  const std::string ind = indent_of(request.indent_cols);
  std::ostringstream out;
  int idx = 1;
  append_line(out, ind, "/**");
  append_line(out, ind, " * @brief " + snippet_ph(idx++, "description"));
  if (is_callable_kind(request.kind)) {
    if (!params.empty()) {
      append_line(out, ind, " *");
      for (const std::string& name : params) {
        append_line(out, ind,
                    " * @param " + name + " " + snippet_ph(idx++, "description"));
        append_line(out, ind, " * @units " + snippet_ph(idx++, "-"));
      }
    }
  } else if (!is_type_kind(request.kind)) {
    append_line(out, ind, " * @units " + snippet_ph(idx++, "-"));
  }
  append_line(out, ind, " */");
  out << "$0";
  return out.str();
}

std::string build_python_doc(const DocCommentRequest& request,
                             const std::vector<std::string>& params) {
  // Indent one level deeper than the def/class line for the docstring body.
  const int body_indent = request.indent_cols + 4;
  const std::string ind = indent_of(body_indent);
  std::ostringstream out;
  int idx = 1;
  append_line(out, ind, "\"\"\"");
  append_line(out, ind, snippet_ph(idx++, "description"));
  if (is_callable_kind(request.kind) && !params.empty()) {
    append_line(out, ind, "");
    for (const std::string& name : params) {
      append_line(out, ind, ":param " + name + ": " + snippet_ph(idx++, "description"));
      append_line(out, ind, ":units " + name + ": " + snippet_ph(idx++, "-"));
    }
  } else if (!is_type_kind(request.kind) && !is_callable_kind(request.kind)) {
    append_line(out, ind, "");
    append_line(out, ind, ":units: " + snippet_ph(idx++, "-"));
  }
  append_line(out, ind, "\"\"\"");
  out << "$0";
  return out.str();
}

std::string build_triple_slash_doc(const DocCommentRequest& request,
                                   const std::vector<std::string>& params) {
  const std::string ind = indent_of(request.indent_cols);
  std::ostringstream out;
  int idx = 1;
  append_line(out, ind, "/// " + snippet_ph(idx++, "description"));
  if (is_callable_kind(request.kind)) {
    for (const std::string& name : params) {
      append_line(out, ind, "///");
      append_line(out, ind, "/// * `" + name + "` - " + snippet_ph(idx++, "description"));
      append_line(out, ind, "/// * units: " + snippet_ph(idx++, "-"));
    }
  } else if (!is_type_kind(request.kind)) {
    append_line(out, ind, "///");
    append_line(out, ind, "/// units: " + snippet_ph(idx++, "-"));
  }
  out << "$0";
  return out.str();
}

std::string build_fortran_doc(const DocCommentRequest& request,
                              const std::vector<std::string>& params) {
  const std::string ind = indent_of(request.indent_cols);
  std::ostringstream out;
  int idx = 1;
  append_line(out, ind, "!> " + snippet_ph(idx++, "description"));
  if (is_callable_kind(request.kind)) {
    for (const std::string& name : params) {
      append_line(out, ind, "!! @param " + name + " " + snippet_ph(idx++, "description"));
      append_line(out, ind, "!! @units " + snippet_ph(idx++, "-"));
    }
  } else if (!is_type_kind(request.kind)) {
    append_line(out, ind, "!! @units " + snippet_ph(idx++, "-"));
  }
  out << "$0";
  return out.str();
}

std::string build_hash_doc(const DocCommentRequest& request,
                           const std::vector<std::string>& params) {
  const std::string ind = indent_of(request.indent_cols);
  std::ostringstream out;
  int idx = 1;
  append_line(out, ind, "# " + snippet_ph(idx++, "description"));
  if (is_callable_kind(request.kind)) {
    for (const std::string& name : params) {
      append_line(out, ind, "# @param " + name + " " + snippet_ph(idx++, "description"));
      append_line(out, ind, "# @units " + snippet_ph(idx++, "-"));
    }
  } else if (!is_type_kind(request.kind)) {
    append_line(out, ind, "# @units " + snippet_ph(idx++, "-"));
  }
  out << "$0";
  return out.str();
}

std::string build_block_separator(const std::string& indent, int width) {
  std::ostringstream out;
  append_line(out, indent, std::string("/*") + std::string(width - 2, '*'));
  append_line(out, indent, pad_banner_line(" * ", snippet_ph(1, "text"), " *", width));
  append_line(out, indent, std::string(" ") + std::string(width - 2, '*') + "/");
  out << "$0";
  return out.str();
}

std::string build_hash_separator(const std::string& indent, int width) {
  std::ostringstream out;
  append_line(out, indent, star_rule("# ", "", width));
  append_line(out, indent, pad_banner_line("# ", snippet_ph(1, "text"), "", width));
  append_line(out, indent, star_rule("# ", "", width));
  out << "$0";
  return out.str();
}

std::string build_bang_separator(const std::string& indent, int width) {
  std::ostringstream out;
  append_line(out, indent, star_rule("! ", "", width));
  append_line(out, indent, pad_banner_line("! ", snippet_ph(1, "text"), "", width));
  append_line(out, indent, star_rule("! ", "", width));
  out << "$0";
  return out.str();
}

std::string build_block_file_header(const std::string& filename, const std::string& date) {
  constexpr int W = kBannerWidth;
  std::ostringstream out;
  int idx = 1;
  const std::string L = " * ";
  const std::string R = " *";

  auto full_rule = [&]() { append_line(out, "", std::string(" ") + std::string(W - 1, '*')); };

  append_line(out, "", std::string("/*") + std::string(W - 2, '*'));
  append_line(out, "", empty_star_row(L, R, W));
  append_line(out, "", labeled_star_row(L, "File:        ", filename, R, W));
  append_line(out, "", empty_star_row(L, R, W));
  full_rule();
  append_line(out, "", empty_star_row(L, R, W));
  append_line(out, "",
              labeled_star_row(L, "Description: ", snippet_ph(idx++, "brief description"), R, W));
  append_line(out, "", empty_star_row(L, R, W));
  append_line(out, "",
              labeled_star_row(L, "Details:     ",
                               snippet_ph(idx++, "longer description / purpose"), R, W));
  append_line(out, "", empty_star_row(L, R, W));
  full_rule();
  append_line(out, "", empty_star_row(L, R, W));
  append_line(out, "",
              labeled_star_row(L, "Author:      ", snippet_ph(idx++, "author"), R, W));
  append_line(out, "",
              labeled_star_row(L, "Created:     ", snippet_ph(idx++, date), R, W));
  append_line(out, "",
              labeled_star_row(L, "Modified:    ", snippet_ph(idx++, date), R, W));
  append_line(out, "", empty_star_row(L, R, W));
  append_line(out, "",
              labeled_star_row(L, "License:     ", snippet_ph(idx++, "license"), R, W));
  append_line(out, "",
              labeled_star_row(L, "Copyright:   ", snippet_ph(idx++, "(C) year author"), R, W));
  append_line(out, "", empty_star_row(L, R, W));
  full_rule();
  append_line(out, "", empty_star_row(L, R, W));
  append_line(out, "",
              labeled_star_row(L, "Notes:       ",
                               snippet_ph(idx++, "notes / dependencies / usage"), R, W));
  append_line(out, "", empty_star_row(L, R, W));
  append_line(out, "", std::string(" ") + std::string(W - 2, '*') + "/");
  out << "$0";
  return out.str();
}

std::string build_line_file_header(const std::string& line_prefix, const std::string& filename,
                                   const std::string& date) {
  constexpr int W = kBannerWidth;
  std::ostringstream out;
  int idx = 1;
  const std::string pref = line_prefix;

  auto full_rule = [&]() {
    append_line(out, "", pref + std::string(W - static_cast<int>(pref.size()), '*'));
  };
  auto blank = [&]() {
    append_line(out, "", pad_banner_line(pref, "", "", W));
  };
  auto field = [&](const std::string& label, const std::string& value) {
    append_line(out, "", pad_banner_line(pref, label + value, "", W));
  };

  full_rule();
  blank();
  field("File:        ", filename);
  blank();
  full_rule();
  blank();
  field("Description: ", snippet_ph(idx++, "brief description"));
  blank();
  field("Details:     ", snippet_ph(idx++, "longer description / purpose"));
  blank();
  full_rule();
  blank();
  field("Author:      ", snippet_ph(idx++, "author"));
  field("Created:     ", snippet_ph(idx++, date));
  field("Modified:    ", snippet_ph(idx++, date));
  blank();
  field("License:     ", snippet_ph(idx++, "license"));
  field("Copyright:   ", snippet_ph(idx++, "(C) year author"));
  blank();
  full_rule();
  blank();
  field("Notes:       ", snippet_ph(idx++, "notes / dependencies / usage"));
  blank();
  full_rule();
  out << "$0";
  return out.str();
}

}  // namespace

std::vector<std::string> extract_param_names(const std::string& declaration_line,
                                             const std::string& language_id) {
  std::vector<std::string> names;
  const std::size_t open = declaration_line.find('(');
  if (open == std::string::npos) {
    return names;
  }
  const std::size_t close = find_matching_close_paren(declaration_line, open);
  if (close == std::string::npos) {
    return names;
  }
  const std::string args =
      trim_copy(declaration_line.substr(open + 1, close - open - 1));
  if (args.empty() || args == "void") {
    return names;
  }

  const bool python = language_id == "python";
  for (const std::string& raw : split_params(args)) {
    if (python) {
      const std::string name = python_param_name(raw);
      if (!skip_python_param(name)) {
        names.push_back(name);
      }
    } else {
      const std::string cleaned = strip_default_argument(raw);
      const std::string name = last_identifier(cleaned);
      if (!name.empty() && name != "void") {
        names.push_back(name);
      }
    }
  }
  return names;
}

std::string build_doc_comment_snippet(const DocCommentRequest& request) {
  const std::string language_id = language_id_for_path(request.path);
  const std::vector<std::string> params =
      extract_param_names(request.declaration_line, language_id);
  switch (style_for_language(language_id)) {
    case DocStyle::kPythonDocstring:
      return build_python_doc(request, params);
    case DocStyle::kTripleSlash:
      return build_triple_slash_doc(request, params);
    case DocStyle::kFortranBang:
      return build_fortran_doc(request, params);
    case DocStyle::kHashBanner:
      return build_hash_doc(request, params);
    case DocStyle::kDoxygenBlock:
    case DocStyle::kPlainBlock:
    default:
      return build_doxygen_doc(request, params);
  }
}

std::string build_separator_snippet(const std::string& path, int indent_cols) {
  const DocStyle style = style_for_path(path);
  const std::string ind = indent_of(indent_cols);
  switch (style) {
    case DocStyle::kPythonDocstring:
    case DocStyle::kHashBanner:
      return build_hash_separator(ind, kBannerWidth);
    case DocStyle::kFortranBang:
      return build_bang_separator(ind, kBannerWidth);
    case DocStyle::kTripleSlash:
    case DocStyle::kDoxygenBlock:
    case DocStyle::kPlainBlock:
    default:
      return build_block_separator(ind, kBannerWidth);
  }
}

std::string build_file_header_snippet(const std::string& path) {
  const DocStyle style = style_for_path(path);
  const std::string filename = filename_of(path);
  const std::string date = today_iso_date();
  switch (style) {
    case DocStyle::kPythonDocstring:
    case DocStyle::kHashBanner:
      return build_line_file_header("# ", filename, date);
    case DocStyle::kFortranBang:
      return build_line_file_header("! ", filename, date);
    case DocStyle::kTripleSlash:
      // Rust file docs often use //!; use block banner for visual consistency.
      return build_block_file_header(filename, date);
    case DocStyle::kDoxygenBlock:
    case DocStyle::kPlainBlock:
    default:
      return build_block_file_header(filename, date);
  }
}

DocCommentInsertPlan plan_doc_comment_insert(const DocCommentRequest& request, int symbol_line) {
  DocCommentInsertPlan plan;
  plan.snippet = build_doc_comment_snippet(request);
  const DocStyle style = style_for_path(request.path);
  if (style == DocStyle::kPythonDocstring) {
    // Insert on the line after the def/class (body).
    plan.insert_line = symbol_line + 1;
    plan.insert_col = 0;
  } else {
    plan.insert_line = symbol_line;
    plan.insert_col = 0;
  }
  return plan;
}

}  // namespace tgdb
