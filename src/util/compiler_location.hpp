#pragma once

#include <optional>
#include <string>

namespace tuide {

struct CompilerLocationMatch {
  std::string path;
  int line = 0;
  int column = 0;
  int span_start = 0;
  int span_end = 0;
};

std::optional<CompilerLocationMatch> find_compiler_location(const std::string& line);

std::string resolve_compiler_path(const std::string& path, const std::string& workspace_root,
                                  const std::string& cwd);

}  // namespace tuide
