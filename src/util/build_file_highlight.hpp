#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"

namespace tgdb {

enum class BuildFileKind {
  kNone,
  kMakefile,
  kCMake,
  kShell,
  kYaml,
  kMarkdown,
  kTex,
  kXml,
};

BuildFileKind detect_build_file_kind(const std::string& path);

ftxui::Element HighlightBuildFileLine(BuildFileKind kind, const std::string& line,
                                      int cursor_col = -1, ftxui::Decorator cursor_style = {});

ftxui::Element HighlightBuildFileLine(const std::string& line, int cursor_col = -1,
                                      ftxui::Decorator cursor_style = {});

}  // namespace tgdb
