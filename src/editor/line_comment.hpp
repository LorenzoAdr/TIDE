#pragma once

#include <string>

namespace tuide {

struct LineCommentStyle {
  std::string prefix;
  std::string suffix;
};

LineCommentStyle line_comment_style_for_path(const std::string& path);

void comment_line_text(std::string* line, const LineCommentStyle& style);
bool uncomment_line_text(std::string* line, const LineCommentStyle& style);
bool line_is_commented(const std::string& line, const LineCommentStyle& style);

}  // namespace tuide
