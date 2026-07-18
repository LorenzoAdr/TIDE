#pragma once

#include <string>
#include <vector>

namespace tuide {

struct HoverParams {
  std::string path;
  std::string text;
  int line = 0;
  int character = 0;
};

struct HoverInfo {
  bool valid = false;
  std::string title;
  std::vector<std::string> body_lines;
};

}  // namespace tuide
