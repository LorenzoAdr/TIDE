#include "parser/tree_sitter_xml_wrap.hpp"

#include <algorithm>

namespace tuide {
namespace {

uint32_t byte_at_point(const std::string& source, uint32_t row, uint32_t column) {
  uint32_t pos = 0;
  uint32_t at_row = 0;
  while (pos < source.size() && at_row < row) {
    if (source[pos] == '\n') {
      ++at_row;
    }
    ++pos;
  }
  if (at_row != row) {
    return static_cast<uint32_t>(source.size());
  }
  const uint32_t line_begin = pos;
  while (pos < source.size() && source[pos] != '\n') {
    ++pos;
  }
  const uint32_t line_len = pos - line_begin;
  return line_begin + std::min(column, line_len);
}

void point_at_byte(const std::string& source, uint32_t byte, uint32_t* row, uint32_t* column) {
  uint32_t r = 0;
  uint32_t line_begin = 0;
  for (uint32_t i = 0; i < byte && i < source.size(); ++i) {
    if (source[i] == '\n') {
      ++r;
      line_begin = i + 1;
    }
  }
  *row = r;
  *column = byte >= line_begin ? byte - line_begin : 0;
}

uint32_t line_byte_length(const std::string& source, int line_0) {
  if (line_0 < 0 || source.empty()) {
    return 0;
  }
  uint32_t pos = 0;
  int row = 0;
  while (pos < source.size() && row < line_0) {
    if (source[pos] == '\n') {
      ++row;
    }
    ++pos;
  }
  if (row != line_0) {
    return 0;
  }
  const uint32_t begin = pos;
  while (pos < source.size() && source[pos] != '\n') {
    ++pos;
  }
  return pos - begin;
}

}  // namespace

bool uses_xml_fragment_wrap(TreeSitterLangKind lang) {
  return lang == TreeSitterLangKind::kXml;
}

uint32_t xml_fragment_inject_point(const std::string& source) {
  std::size_t i = 0;
  // UTF-8 BOM
  if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
      static_cast<unsigned char>(source[1]) == 0xBB &&
      static_cast<unsigned char>(source[2]) == 0xBF) {
    i = 3;
  }
  while (i < source.size() &&
         (source[i] == ' ' || source[i] == '\t' || source[i] == '\r' || source[i] == '\n')) {
    ++i;
  }
  if (i + 5 > source.size()) {
    return 0;
  }
  if (!(source[i] == '<' && source[i + 1] == '?' && (source[i + 2] == 'x' || source[i + 2] == 'X') &&
        (source[i + 3] == 'm' || source[i + 3] == 'M') &&
        (source[i + 4] == 'l' || source[i + 4] == 'L'))) {
    return 0;
  }
  const std::size_t close = source.find("?>", i + 5);
  if (close == std::string::npos) {
    return 0;
  }
  return static_cast<uint32_t>(close + 2);
}

XmlFragmentWrap xml_wrap_fragment_source(const std::string& source) {
  XmlFragmentWrap wrap;
  wrap.inject_point = xml_fragment_inject_point(source);
  wrap.open_len = static_cast<uint32_t>(sizeof(kXmlFragmentRootOpen) - 1);
  wrap.source_size = static_cast<uint32_t>(source.size());
  wrap.wrapped.reserve(source.size() + wrap.open_len + (sizeof(kXmlFragmentRootClose) - 1));
  wrap.wrapped.append(source, 0, wrap.inject_point);
  wrap.wrapped.append(kXmlFragmentRootOpen);
  wrap.wrapped.append(source, wrap.inject_point, std::string::npos);
  wrap.wrapped.append(kXmlFragmentRootClose);
  return wrap;
}

std::size_t xml_wrapped_byte_to_original(const XmlFragmentWrap& wrap, uint32_t wrapped_byte) {
  if (!wrap.active()) {
    return wrapped_byte;
  }
  if (wrapped_byte < wrap.inject_point) {
    return wrapped_byte;
  }
  if (wrapped_byte < wrap.inject_point + wrap.open_len) {
    return std::string::npos;
  }
  const uint32_t original = wrapped_byte - wrap.open_len;
  if (original > wrap.source_size) {
    return std::string::npos;
  }
  return original;
}

uint32_t xml_original_byte_to_wrapped(const XmlFragmentWrap& wrap, uint32_t original_byte) {
  if (!wrap.active()) {
    return original_byte;
  }
  if (original_byte < wrap.inject_point) {
    return original_byte;
  }
  return original_byte + wrap.open_len;
}

void xml_unmap_line_highlights_from_wrap(LineHighlights* highlights, int line_0,
                                         const XmlFragmentWrap& wrap, const std::string& source) {
  if (highlights == nullptr || !wrap.active()) {
    return;
  }
  const uint32_t line_len = line_byte_length(source, line_0);
  LineHighlights out;
  out.spans.reserve(highlights->spans.size());
  for (const HighlightSpan& span : highlights->spans) {
    const uint32_t start_b =
        byte_at_point(wrap.wrapped, static_cast<uint32_t>(line_0),
                      static_cast<uint32_t>(std::max(0, span.start_col)));
    const uint32_t end_b =
        byte_at_point(wrap.wrapped, static_cast<uint32_t>(line_0),
                      static_cast<uint32_t>(std::max(0, span.end_col)));
    const std::size_t orig_start = xml_wrapped_byte_to_original(wrap, start_b);
    const std::size_t orig_end = xml_wrapped_byte_to_original(wrap, end_b);
    if (orig_start == std::string::npos && orig_end == std::string::npos) {
      continue;
    }
    uint32_t mapped_start = 0;
    uint32_t mapped_end = 0;
    if (orig_start == std::string::npos) {
      // Span started in the synthetic open tag; clamp to first original byte on this line.
      mapped_start = byte_at_point(source, static_cast<uint32_t>(line_0), 0);
      if (line_0 == 0 && wrap.inject_point > 0) {
        // Prefer content after the preserved prefix on line 0.
        uint32_t r = 0;
        uint32_t c = 0;
        point_at_byte(source, wrap.inject_point, &r, &c);
        if (static_cast<int>(r) == line_0) {
          mapped_start = wrap.inject_point;
        }
      }
    } else {
      mapped_start = static_cast<uint32_t>(orig_start);
    }
    if (orig_end == std::string::npos) {
      mapped_end = byte_at_point(source, static_cast<uint32_t>(line_0), line_len);
    } else {
      mapped_end = static_cast<uint32_t>(orig_end);
    }
    if (mapped_end <= mapped_start) {
      continue;
    }
    uint32_t start_row = 0;
    uint32_t start_col = 0;
    uint32_t end_row = 0;
    uint32_t end_col = 0;
    point_at_byte(source, mapped_start, &start_row, &start_col);
    point_at_byte(source, mapped_end, &end_row, &end_col);
    if (static_cast<int>(start_row) != line_0) {
      start_col = 0;
    }
    if (static_cast<int>(end_row) != line_0) {
      end_col = line_len;
    }
    if (end_col <= start_col) {
      continue;
    }
    HighlightSpan mapped = span;
    mapped.start_col = static_cast<int>(start_col);
    mapped.end_col = static_cast<int>(end_col);
    out.spans.push_back(std::move(mapped));
  }
  *highlights = std::move(out);
}

void xml_unmap_highlights_from_wrap(std::vector<LineHighlights>* highlights,
                                    const XmlFragmentWrap& wrap, const std::string& source) {
  if (highlights == nullptr || !wrap.active()) {
    return;
  }
  for (int line = 0; line < static_cast<int>(highlights->size()); ++line) {
    xml_unmap_line_highlights_from_wrap(&(*highlights)[static_cast<std::size_t>(line)], line, wrap,
                                        source);
  }
}

}  // namespace tuide
