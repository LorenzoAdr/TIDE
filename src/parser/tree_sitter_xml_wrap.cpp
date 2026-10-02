#include "parser/tree_sitter_xml_wrap.hpp"

#include <algorithm>

namespace tuide {
namespace {

// Line-start table for one buffer. Every lookup below used to rescan from byte 0, and the
// unmapping does several per highlight span, which made it quadratic in the file size.
struct LineIndex {
  explicit LineIndex(const std::string& source) : size(static_cast<uint32_t>(source.size())) {
    starts.push_back(0);
    for (uint32_t i = 0; i < size; ++i) {
      if (source[i] == '\n') {
        starts.push_back(i + 1);
      }
    }
  }

  // Byte offset of (row, column), with column clamped to the line; buffer end when the row
  // does not exist.
  uint32_t byte_at_point(uint32_t row, uint32_t column) const {
    if (row >= starts.size()) {
      return size;
    }
    const uint32_t begin = starts[row];
    return begin + std::min(column, row_length(row));
  }

  void point_at_byte(uint32_t byte, uint32_t* row, uint32_t* column) const {
    // starts[0] == 0 <= byte, so the iterator is never begin().
    const auto it = std::upper_bound(starts.begin(), starts.end(), byte);
    const uint32_t r = static_cast<uint32_t>(it - starts.begin()) - 1;
    *row = r;
    *column = byte - starts[r];
  }

  uint32_t line_byte_length(int line_0) const {
    if (line_0 < 0 || size == 0 || static_cast<std::size_t>(line_0) >= starts.size()) {
      return 0;
    }
    return row_length(static_cast<uint32_t>(line_0));
  }

 private:
  uint32_t row_length(uint32_t row) const {
    const uint32_t end = row + 1 < starts.size() ? starts[row + 1] - 1 : size;
    return end - starts[row];
  }

  std::vector<uint32_t> starts;
  uint32_t size = 0;
};

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

namespace {

void unmap_line_highlights(LineHighlights* highlights, int line_0, const XmlFragmentWrap& wrap,
                           const LineIndex& wrapped_index, const LineIndex& source_index) {
  if (highlights == nullptr || !wrap.active()) {
    return;
  }
  const uint32_t line_len = source_index.line_byte_length(line_0);
  LineHighlights out;
  out.spans.reserve(highlights->spans.size());
  for (const HighlightSpan& span : highlights->spans) {
    const uint32_t start_b = wrapped_index.byte_at_point(
        static_cast<uint32_t>(line_0), static_cast<uint32_t>(std::max(0, span.start_col)));
    const uint32_t end_b = wrapped_index.byte_at_point(
        static_cast<uint32_t>(line_0), static_cast<uint32_t>(std::max(0, span.end_col)));
    const std::size_t orig_start = xml_wrapped_byte_to_original(wrap, start_b);
    const std::size_t orig_end = xml_wrapped_byte_to_original(wrap, end_b);
    if (orig_start == std::string::npos && orig_end == std::string::npos) {
      continue;
    }
    uint32_t mapped_start = 0;
    uint32_t mapped_end = 0;
    if (orig_start == std::string::npos) {
      // Span started in the synthetic open tag; clamp to first original byte on this line.
      mapped_start = source_index.byte_at_point(static_cast<uint32_t>(line_0), 0);
      if (line_0 == 0 && wrap.inject_point > 0) {
        // Prefer content after the preserved prefix on line 0.
        uint32_t r = 0;
        uint32_t c = 0;
        source_index.point_at_byte(wrap.inject_point, &r, &c);
        if (static_cast<int>(r) == line_0) {
          mapped_start = wrap.inject_point;
        }
      }
    } else {
      mapped_start = static_cast<uint32_t>(orig_start);
    }
    if (orig_end == std::string::npos) {
      mapped_end = source_index.byte_at_point(static_cast<uint32_t>(line_0), line_len);
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
    source_index.point_at_byte(mapped_start, &start_row, &start_col);
    source_index.point_at_byte(mapped_end, &end_row, &end_col);
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

}  // namespace

void xml_unmap_line_highlights_from_wrap(LineHighlights* highlights, int line_0,
                                         const XmlFragmentWrap& wrap, const std::string& source) {
  if (highlights == nullptr || !wrap.active()) {
    return;
  }
  unmap_line_highlights(highlights, line_0, wrap, LineIndex(wrap.wrapped), LineIndex(source));
}

void xml_unmap_highlights_from_wrap(std::vector<LineHighlights>* highlights,
                                    const XmlFragmentWrap& wrap, const std::string& source,
                                    int first_line_0) {
  if (highlights == nullptr || !wrap.active()) {
    return;
  }
  const LineIndex wrapped_index(wrap.wrapped);
  const LineIndex source_index(source);
  for (int i = 0; i < static_cast<int>(highlights->size()); ++i) {
    unmap_line_highlights(&(*highlights)[static_cast<std::size_t>(i)], first_line_0 + i, wrap,
                          wrapped_index, source_index);
  }
}

}  // namespace tuide
