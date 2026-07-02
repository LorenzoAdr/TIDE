#pragma once

#include <string>
#include <vector>

namespace tgdb {

enum class ClangBasedOnStyle {
  kLLVM,
  kGoogle,
  kChromium,
  kMozilla,
  kWebKit,
  kMicrosoft,
  kGNU,
  kCustom,
};

enum class ClangUseTab {
  kNever,
  kForIndentation,
  kForContinuationAndIndentation,
  kAlways,
};

enum class ClangBreakBeforeBraces {
  kAttach,
  kLinux,
  kAllman,
  kStroustrup,
  kGNU,
};

enum class ClangPointerAlignment {
  kLeft,
  kRight,
  kMiddle,
};

enum class ClangIncludeBlocks {
  kPreserve,
  kMerge,
  kRegroup,
};

enum class ClangShortFunctionsOnASingleLine {
  kNone,
  kInlineOnly,
  kEmpty,
  kAll,
};

struct ClangFormatConfig {
  ClangBasedOnStyle based_on_style = ClangBasedOnStyle::kLLVM;
  int indent_width = 4;
  int tab_width = 4;
  ClangUseTab use_tab = ClangUseTab::kNever;
  int column_limit = 80;
  ClangBreakBeforeBraces break_before_braces = ClangBreakBeforeBraces::kAttach;
  ClangPointerAlignment pointer_alignment = ClangPointerAlignment::kRight;
  ClangPointerAlignment reference_alignment = ClangPointerAlignment::kRight;
  bool sort_includes = true;
  ClangIncludeBlocks include_blocks = ClangIncludeBlocks::kPreserve;
  bool indent_case_labels = false;
  ClangShortFunctionsOnASingleLine allow_short_functions_on_a_single_line =
      ClangShortFunctionsOnASingleLine::kInlineOnly;

  std::vector<std::string> preserved_lines;

  int effective_tab_width() const;
  bool uses_tab_char() const;
};

namespace editor_indent {

int width();
bool use_tab_char();
void apply(const ClangFormatConfig& config);

}  // namespace editor_indent

ClangFormatConfig default_clang_format_config();

std::string clang_format_path(const std::string& workspace_root);
ClangFormatConfig load_clang_format(const std::string& workspace_root);
bool save_clang_format(const std::string& workspace_root, const ClangFormatConfig& config);

const char* clang_based_on_style_name(ClangBasedOnStyle style);
const char* clang_use_tab_name(ClangUseTab value);
const char* clang_break_before_braces_name(ClangBreakBeforeBraces value);
const char* clang_pointer_alignment_name(ClangPointerAlignment value);
const char* clang_include_blocks_name(ClangIncludeBlocks value);
const char* clang_short_functions_name(ClangShortFunctionsOnASingleLine value);

void cycle_clang_based_on_style(ClangFormatConfig* config);
void cycle_clang_indent_width(ClangFormatConfig* config);
void cycle_clang_use_tab(ClangFormatConfig* config);
void cycle_clang_tab_width(ClangFormatConfig* config);
void cycle_clang_column_limit(ClangFormatConfig* config);
void cycle_clang_break_before_braces(ClangFormatConfig* config);
void cycle_clang_pointer_alignment(ClangFormatConfig* config);
void cycle_clang_include_blocks(ClangFormatConfig* config);
void cycle_clang_short_functions(ClangFormatConfig* config);

}  // namespace tgdb
