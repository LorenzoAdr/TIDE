#include "lsp/lsp_position.hpp"

namespace tuide {

int lsp_utf16_column(const std::string& line, int byte_col) {
  int utf16 = 0;
  int byte = 0;
  const int limit = byte_col < 0 ? 0 : byte_col;
  while (byte < limit && byte < static_cast<int>(line.size())) {
    const unsigned char c = static_cast<unsigned char>(line[byte]);
    if ((c & 0xF8) == 0xF0) {
      byte += 4;
      utf16 += 2;
    } else if ((c & 0xF0) == 0xE0) {
      byte += 3;
      utf16 += 1;
    } else if ((c & 0xE0) == 0xC0) {
      byte += 2;
      utf16 += 1;
    } else {
      byte += 1;
      utf16 += 1;
    }
  }
  return utf16;
}

std::string line_text_at(const std::string& text, int line) {
  if (line < 0) {
    return {};
  }
  int current = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      if (current == line) {
        return text.substr(start, i - start);
      }
      ++current;
      start = i + 1;
    }
  }
  return {};
}

nlohmann::json make_lsp_position(const std::string& text, int line, int byte_col) {
  const std::string line_text = line_text_at(text, line);
  return {{"line", line}, {"character", lsp_utf16_column(line_text, byte_col)}};
}

}  // namespace tuide
