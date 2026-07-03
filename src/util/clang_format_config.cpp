#include "util/clang_format_config.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace tgdb {

namespace fs = std::filesystem;

namespace {

constexpr const char* kManagedKeys[] = {
    "BasedOnStyle",
    "IndentWidth",
    "TabWidth",
    "UseTab",
    "ColumnLimit",
    "BreakBeforeBraces",
    "PointerAlignment",
    "ReferenceAlignment",
    "SortIncludes",
    "IncludeBlocks",
    "IndentCaseLabels",
    "AllowShortFunctionsOnASingleLine",
};

bool is_managed_key(const std::string& key) {
  for (const char* managed : kManagedKeys) {
    if (key == managed) {
      return true;
    }
  }
  return false;
}

std::string trim(const std::string& value) {
  std::size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

std::string unquote(const std::string& value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

bool parse_bool(const std::string& value, bool* out) {
  const std::string lower = [&] {
    std::string copy = value;
    std::transform(copy.begin(), copy.end(), copy.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return copy;
  }();
  if (lower == "true" || lower == "yes" || lower == "1") {
    *out = true;
    return true;
  }
  if (lower == "false" || lower == "no" || lower == "0") {
    *out = false;
    return true;
  }
  return false;
}

bool parse_int(const std::string& value, int* out) {
  if (value.empty()) {
    return false;
  }
  try {
    std::size_t consumed = 0;
    const int parsed = std::stoi(value, &consumed);
    if (consumed != value.size()) {
      return false;
    }
    *out = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

ClangBasedOnStyle parse_based_on_style(const std::string& value) {
  if (value == "LLVM") {
    return ClangBasedOnStyle::kLLVM;
  }
  if (value == "Google") {
    return ClangBasedOnStyle::kGoogle;
  }
  if (value == "Chromium") {
    return ClangBasedOnStyle::kChromium;
  }
  if (value == "Mozilla") {
    return ClangBasedOnStyle::kMozilla;
  }
  if (value == "WebKit") {
    return ClangBasedOnStyle::kWebKit;
  }
  if (value == "Microsoft") {
    return ClangBasedOnStyle::kMicrosoft;
  }
  if (value == "GNU") {
    return ClangBasedOnStyle::kGNU;
  }
  return ClangBasedOnStyle::kCustom;
}

ClangUseTab parse_use_tab(const std::string& value) {
  if (value == "Never") {
    return ClangUseTab::kNever;
  }
  if (value == "ForIndentation") {
    return ClangUseTab::kForIndentation;
  }
  if (value == "ForContinuationAndIndentation") {
    return ClangUseTab::kForContinuationAndIndentation;
  }
  if (value == "Always") {
    return ClangUseTab::kAlways;
  }
  return ClangUseTab::kNever;
}

ClangBreakBeforeBraces parse_break_before_braces(const std::string& value) {
  if (value == "Attach") {
    return ClangBreakBeforeBraces::kAttach;
  }
  if (value == "Linux") {
    return ClangBreakBeforeBraces::kLinux;
  }
  if (value == "Allman") {
    return ClangBreakBeforeBraces::kAllman;
  }
  if (value == "Stroustrup") {
    return ClangBreakBeforeBraces::kStroustrup;
  }
  if (value == "GNU") {
    return ClangBreakBeforeBraces::kGNU;
  }
  return ClangBreakBeforeBraces::kAttach;
}

ClangPointerAlignment parse_pointer_alignment(const std::string& value) {
  if (value == "Left") {
    return ClangPointerAlignment::kLeft;
  }
  if (value == "Middle") {
    return ClangPointerAlignment::kMiddle;
  }
  return ClangPointerAlignment::kRight;
}

ClangIncludeBlocks parse_include_blocks(const std::string& value) {
  if (value == "Merge") {
    return ClangIncludeBlocks::kMerge;
  }
  if (value == "Regroup") {
    return ClangIncludeBlocks::kRegroup;
  }
  return ClangIncludeBlocks::kPreserve;
}

ClangShortFunctionsOnASingleLine parse_short_functions(const std::string& value) {
  if (value == "None") {
    return ClangShortFunctionsOnASingleLine::kNone;
  }
  if (value == "Empty") {
    return ClangShortFunctionsOnASingleLine::kEmpty;
  }
  if (value == "All") {
    return ClangShortFunctionsOnASingleLine::kAll;
  }
  if (value == "Inline" || value == "InlineOnly") {
    return ClangShortFunctionsOnASingleLine::kInlineOnly;
  }
  return ClangShortFunctionsOnASingleLine::kInlineOnly;
}

void apply_key_value(ClangFormatConfig* config, const std::string& key,
                     const std::string& raw_value) {
  if (config == nullptr) {
    return;
  }
  const std::string value = unquote(trim(raw_value));
  if (key == "BasedOnStyle") {
    config->based_on_style = parse_based_on_style(value);
    return;
  }
  if (key == "IndentWidth") {
    int parsed = config->indent_width;
    if (parse_int(value, &parsed)) {
      config->indent_width = std::max(1, parsed);
    }
    return;
  }
  if (key == "TabWidth") {
    int parsed = config->tab_width;
    if (parse_int(value, &parsed)) {
      config->tab_width = std::max(1, parsed);
    }
    return;
  }
  if (key == "UseTab") {
    config->use_tab = parse_use_tab(value);
    return;
  }
  if (key == "ColumnLimit") {
    int parsed = config->column_limit;
    if (parse_int(value, &parsed)) {
      config->column_limit = std::max(0, parsed);
    }
    return;
  }
  if (key == "BreakBeforeBraces") {
    config->break_before_braces = parse_break_before_braces(value);
    return;
  }
  if (key == "PointerAlignment") {
    config->pointer_alignment = parse_pointer_alignment(value);
    return;
  }
  if (key == "ReferenceAlignment") {
    config->reference_alignment = parse_pointer_alignment(value);
    return;
  }
  if (key == "SortIncludes") {
    parse_bool(value, &config->sort_includes);
    return;
  }
  if (key == "IncludeBlocks") {
    config->include_blocks = parse_include_blocks(value);
    return;
  }
  if (key == "IndentCaseLabels") {
    parse_bool(value, &config->indent_case_labels);
    return;
  }
  if (key == "AllowShortFunctionsOnASingleLine") {
    config->allow_short_functions_on_a_single_line = parse_short_functions(value);
  }
}

void write_line(std::ostream& out, const std::string& key, const std::string& value) {
  out << key << ": " << value << '\n';
}

void write_line(std::ostream& out, const std::string& key, int value) {
  out << key << ": " << value << '\n';
}

void write_line(std::ostream& out, const std::string& key, bool value) {
  out << key << ": " << (value ? "true" : "false") << '\n';
}

}  // namespace

int default_indent_width_for_style(ClangBasedOnStyle style) {
  switch (style) {
    case ClangBasedOnStyle::kChromium:
    case ClangBasedOnStyle::kWebKit:
    case ClangBasedOnStyle::kMicrosoft:
      return 4;
    case ClangBasedOnStyle::kLLVM:
    case ClangBasedOnStyle::kGoogle:
    case ClangBasedOnStyle::kMozilla:
    case ClangBasedOnStyle::kGNU:
    case ClangBasedOnStyle::kCustom:
      return 2;
  }
  return 2;
}

void normalize_clang_format_config(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  config->indent_width = std::max(1, config->indent_width);
  if (!config->uses_tab_char() || config->tab_width <= 0) {
    config->tab_width = config->indent_width;
  } else {
    config->tab_width = std::max(1, config->tab_width);
  }
}

int ClangFormatConfig::effective_tab_width() const {
  return uses_tab_char() ? std::max(1, tab_width) : std::max(1, indent_width);
}

bool ClangFormatConfig::uses_tab_char() const {
  return use_tab != ClangUseTab::kNever;
}

namespace editor_indent {

namespace {

int g_indent_width = 4;
int g_tab_display_width = 4;
bool g_use_tab = false;

}  // namespace

int width() { return g_indent_width; }

int tab_display_width() { return g_tab_display_width; }

bool use_tab_char() { return g_use_tab; }

void apply(const ClangFormatConfig& config) {
  g_indent_width = std::max(1, config.indent_width);
  g_tab_display_width = std::max(1, config.tab_width);
  g_use_tab = config.uses_tab_char();
}

}  // namespace editor_indent

ClangFormatConfig default_clang_format_config() {
  ClangFormatConfig config;
  config.indent_width = default_indent_width_for_style(config.based_on_style);
  config.tab_width = config.indent_width;
  return config;
}

std::string clang_format_path(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }
  return (fs::path(workspace_root) / ".clang-format").string();
}

std::string clang_format_root_for_file(const std::string& absolute_path) {
  if (absolute_path.empty()) {
    return {};
  }
  fs::path dir = fs::path(absolute_path).parent_path();
  std::error_code ec;
  while (!dir.empty()) {
    if (fs::is_regular_file(dir / ".clang-format", ec)) {
      return dir.string();
    }
    const fs::path parent = dir.parent_path();
    if (parent == dir) {
      break;
    }
    dir = parent;
  }
  return {};
}

ClangFormatConfig load_clang_format_for_file(const std::string& absolute_path,
                                             const std::string& workspace_root) {
  const std::string from_file = clang_format_root_for_file(absolute_path);
  if (!from_file.empty()) {
    return load_clang_format_from_disk(from_file);
  }
  if (!workspace_root.empty()) {
    return load_clang_format_from_disk(workspace_root);
  }
  return default_clang_format_config();
}

ClangFormatConfig load_clang_format_from_disk(const std::string& workspace_root) {
  ClangFormatConfig config = default_clang_format_config();
  const std::string path = clang_format_path(workspace_root);
  if (path.empty()) {
    normalize_clang_format_config(&config);
    return config;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    normalize_clang_format_config(&config);
    return config;
  }

  std::string line;
  bool indent_width_explicit = false;
  while (std::getline(input, line)) {
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }
    if (trimmed == "---") {
      continue;
    }

    const std::size_t colon = trimmed.find(':');
    if (colon == std::string::npos) {
      config.preserved_lines.push_back(line);
      continue;
    }

    const std::string key = trim(trimmed.substr(0, colon));
    const std::string value = trim(trimmed.substr(colon + 1));
    if (is_managed_key(key)) {
      if (key == "IndentWidth") {
        indent_width_explicit = true;
      }
      apply_key_value(&config, key, value);
    } else {
      config.preserved_lines.push_back(line);
    }
  }

  if (!indent_width_explicit) {
    config.indent_width = default_indent_width_for_style(config.based_on_style);
  }
  normalize_clang_format_config(&config);
  return config;
}

ClangFormatConfig load_clang_format(const std::string& workspace_root) {
  ClangFormatConfig config = load_clang_format_from_disk(workspace_root);
  editor_indent::apply(config);
  return config;
}

bool sync_clang_format_file_for_formatting(const std::string& workspace_root,
                                           const ClangFormatConfig* active_config) {
  if (workspace_root.empty()) {
    return false;
  }

  ClangFormatConfig config =
      active_config != nullptr ? *active_config : load_clang_format_from_disk(workspace_root);

  if (active_config == nullptr) {
    config.indent_width = std::max(1, editor_indent::width());
    if (!editor_indent::use_tab_char()) {
      config.use_tab = ClangUseTab::kNever;
      config.tab_width = config.indent_width;
    } else {
      if (config.use_tab == ClangUseTab::kNever) {
        config.use_tab = ClangUseTab::kForIndentation;
      }
      config.tab_width = std::max(1, editor_indent::tab_display_width());
    }
  }

  normalize_clang_format_config(&config);
  return save_clang_format(workspace_root, config);
}

bool save_clang_format(const std::string& workspace_root, const ClangFormatConfig& config) {
  const std::string path = clang_format_path(workspace_root);
  if (path.empty()) {
    return false;
  }

  ClangFormatConfig normalized = config;
  normalize_clang_format_config(&normalized);

  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);

  std::ofstream output(path, std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  if (normalized.based_on_style != ClangBasedOnStyle::kCustom) {
    write_line(output, "BasedOnStyle", clang_based_on_style_name(normalized.based_on_style));
  }
  write_line(output, "IndentWidth", normalized.indent_width);
  write_line(output, "TabWidth", normalized.tab_width);
  write_line(output, "UseTab", clang_use_tab_name(normalized.use_tab));
  write_line(output, "ColumnLimit", normalized.column_limit);
  write_line(output, "BreakBeforeBraces",
             clang_break_before_braces_name(normalized.break_before_braces));
  write_line(output, "PointerAlignment",
             clang_pointer_alignment_name(normalized.pointer_alignment));
  write_line(output, "ReferenceAlignment",
             clang_pointer_alignment_name(normalized.reference_alignment));
  write_line(output, "SortIncludes", normalized.sort_includes);
  write_line(output, "IncludeBlocks", clang_include_blocks_name(normalized.include_blocks));
  write_line(output, "IndentCaseLabels", normalized.indent_case_labels);
  write_line(output, "AllowShortFunctionsOnASingleLine",
             clang_short_functions_name(normalized.allow_short_functions_on_a_single_line));

  if (!normalized.preserved_lines.empty()) {
    output << '\n';
    for (const std::string& preserved : normalized.preserved_lines) {
      output << preserved << '\n';
    }
  }

  editor_indent::apply(normalized);
  return true;
}

const char* clang_based_on_style_name(ClangBasedOnStyle style) {
  switch (style) {
    case ClangBasedOnStyle::kLLVM:
      return "LLVM";
    case ClangBasedOnStyle::kGoogle:
      return "Google";
    case ClangBasedOnStyle::kChromium:
      return "Chromium";
    case ClangBasedOnStyle::kMozilla:
      return "Mozilla";
    case ClangBasedOnStyle::kWebKit:
      return "WebKit";
    case ClangBasedOnStyle::kMicrosoft:
      return "Microsoft";
    case ClangBasedOnStyle::kGNU:
      return "GNU";
    case ClangBasedOnStyle::kCustom:
      return "Personalizado";
  }
  return "LLVM";
}

const char* clang_use_tab_name(ClangUseTab value) {
  switch (value) {
    case ClangUseTab::kNever:
      return "Never";
    case ClangUseTab::kForIndentation:
      return "ForIndentation";
    case ClangUseTab::kForContinuationAndIndentation:
      return "ForContinuationAndIndentation";
    case ClangUseTab::kAlways:
      return "Always";
  }
  return "Never";
}

const char* clang_break_before_braces_name(ClangBreakBeforeBraces value) {
  switch (value) {
    case ClangBreakBeforeBraces::kAttach:
      return "Attach";
    case ClangBreakBeforeBraces::kLinux:
      return "Linux";
    case ClangBreakBeforeBraces::kAllman:
      return "Allman";
    case ClangBreakBeforeBraces::kStroustrup:
      return "Stroustrup";
    case ClangBreakBeforeBraces::kGNU:
      return "GNU";
  }
  return "Attach";
}

const char* clang_pointer_alignment_name(ClangPointerAlignment value) {
  switch (value) {
    case ClangPointerAlignment::kLeft:
      return "Left";
    case ClangPointerAlignment::kMiddle:
      return "Middle";
    case ClangPointerAlignment::kRight:
      return "Right";
  }
  return "Right";
}

const char* clang_include_blocks_name(ClangIncludeBlocks value) {
  switch (value) {
    case ClangIncludeBlocks::kPreserve:
      return "Preserve";
    case ClangIncludeBlocks::kMerge:
      return "Merge";
    case ClangIncludeBlocks::kRegroup:
      return "Regroup";
  }
  return "Preserve";
}

const char* clang_short_functions_name(ClangShortFunctionsOnASingleLine value) {
  switch (value) {
    case ClangShortFunctionsOnASingleLine::kNone:
      return "None";
    case ClangShortFunctionsOnASingleLine::kInlineOnly:
      return "InlineOnly";
    case ClangShortFunctionsOnASingleLine::kEmpty:
      return "Empty";
    case ClangShortFunctionsOnASingleLine::kAll:
      return "All";
  }
  return "InlineOnly";
}

void cycle_clang_based_on_style(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  switch (config->based_on_style) {
    case ClangBasedOnStyle::kLLVM:
      config->based_on_style = ClangBasedOnStyle::kGoogle;
      break;
    case ClangBasedOnStyle::kGoogle:
      config->based_on_style = ClangBasedOnStyle::kChromium;
      break;
    case ClangBasedOnStyle::kChromium:
      config->based_on_style = ClangBasedOnStyle::kMozilla;
      break;
    case ClangBasedOnStyle::kMozilla:
      config->based_on_style = ClangBasedOnStyle::kWebKit;
      break;
    case ClangBasedOnStyle::kWebKit:
      config->based_on_style = ClangBasedOnStyle::kMicrosoft;
      break;
    case ClangBasedOnStyle::kMicrosoft:
      config->based_on_style = ClangBasedOnStyle::kGNU;
      break;
    case ClangBasedOnStyle::kGNU:
      config->based_on_style = ClangBasedOnStyle::kCustom;
      break;
    case ClangBasedOnStyle::kCustom:
      config->based_on_style = ClangBasedOnStyle::kLLVM;
      break;
  }
}

void cycle_clang_indent_width(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  switch (config->indent_width) {
    case 2:
      config->indent_width = 4;
      break;
    case 4:
      config->indent_width = 8;
      break;
    default:
      config->indent_width = 2;
      break;
  }
  if (config->use_tab == ClangUseTab::kNever) {
    config->tab_width = config->indent_width;
  }
}

void cycle_clang_use_tab(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  switch (config->use_tab) {
    case ClangUseTab::kNever:
      config->use_tab = ClangUseTab::kForIndentation;
      break;
    case ClangUseTab::kForIndentation:
      config->use_tab = ClangUseTab::kForContinuationAndIndentation;
      break;
    case ClangUseTab::kForContinuationAndIndentation:
      config->use_tab = ClangUseTab::kAlways;
      break;
    case ClangUseTab::kAlways:
      config->use_tab = ClangUseTab::kNever;
      break;
  }
}

void cycle_clang_tab_width(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  switch (config->tab_width) {
    case 2:
      config->tab_width = 4;
      break;
    case 4:
      config->tab_width = 8;
      break;
    default:
      config->tab_width = 2;
      break;
  }
  if (config->use_tab == ClangUseTab::kNever) {
    config->indent_width = config->tab_width;
  }
}

void cycle_clang_column_limit(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  switch (config->column_limit) {
    case 80:
      config->column_limit = 100;
      break;
    case 100:
      config->column_limit = 120;
      break;
    case 120:
      config->column_limit = 0;
      break;
    default:
      config->column_limit = 80;
      break;
  }
}

void cycle_clang_break_before_braces(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  switch (config->break_before_braces) {
    case ClangBreakBeforeBraces::kAttach:
      config->break_before_braces = ClangBreakBeforeBraces::kLinux;
      break;
    case ClangBreakBeforeBraces::kLinux:
      config->break_before_braces = ClangBreakBeforeBraces::kAllman;
      break;
    case ClangBreakBeforeBraces::kAllman:
      config->break_before_braces = ClangBreakBeforeBraces::kStroustrup;
      break;
    case ClangBreakBeforeBraces::kStroustrup:
      config->break_before_braces = ClangBreakBeforeBraces::kGNU;
      break;
    case ClangBreakBeforeBraces::kGNU:
      config->break_before_braces = ClangBreakBeforeBraces::kAttach;
      break;
  }
}

void cycle_clang_pointer_alignment(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  switch (config->pointer_alignment) {
    case ClangPointerAlignment::kLeft:
      config->pointer_alignment = ClangPointerAlignment::kRight;
      config->reference_alignment = ClangPointerAlignment::kRight;
      break;
    case ClangPointerAlignment::kRight:
      config->pointer_alignment = ClangPointerAlignment::kMiddle;
      config->reference_alignment = ClangPointerAlignment::kMiddle;
      break;
    case ClangPointerAlignment::kMiddle:
      config->pointer_alignment = ClangPointerAlignment::kLeft;
      config->reference_alignment = ClangPointerAlignment::kLeft;
      break;
  }
}

void cycle_clang_include_blocks(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  switch (config->include_blocks) {
    case ClangIncludeBlocks::kPreserve:
      config->include_blocks = ClangIncludeBlocks::kMerge;
      break;
    case ClangIncludeBlocks::kMerge:
      config->include_blocks = ClangIncludeBlocks::kRegroup;
      break;
    case ClangIncludeBlocks::kRegroup:
      config->include_blocks = ClangIncludeBlocks::kPreserve;
      break;
  }
}

void cycle_clang_short_functions(ClangFormatConfig* config) {
  if (config == nullptr) {
    return;
  }
  switch (config->allow_short_functions_on_a_single_line) {
    case ClangShortFunctionsOnASingleLine::kNone:
      config->allow_short_functions_on_a_single_line = ClangShortFunctionsOnASingleLine::kInlineOnly;
      break;
    case ClangShortFunctionsOnASingleLine::kInlineOnly:
      config->allow_short_functions_on_a_single_line = ClangShortFunctionsOnASingleLine::kEmpty;
      break;
    case ClangShortFunctionsOnASingleLine::kEmpty:
      config->allow_short_functions_on_a_single_line = ClangShortFunctionsOnASingleLine::kAll;
      break;
    case ClangShortFunctionsOnASingleLine::kAll:
      config->allow_short_functions_on_a_single_line = ClangShortFunctionsOnASingleLine::kNone;
      break;
  }
}

}  // namespace tgdb
