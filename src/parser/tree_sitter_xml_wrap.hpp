#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_language.hpp"

namespace tuide {

// Invisible synthetic root so multi-root XML fragments parse without ERROR nodes.
// Open/close tags contain no newlines so editor line indices stay aligned.
inline constexpr const char kXmlFragmentRootName[] = "tuide_xml_root";
inline constexpr const char kXmlFragmentRootOpen[] = "<tuide_xml_root>";
inline constexpr const char kXmlFragmentRootClose[] = "</tuide_xml_root>";

struct XmlFragmentWrap {
  std::string wrapped;
  // Byte offset in `wrapped` where the original `source` body continues after the
  // injected open tag. Bytes [0, inject_point) are a verbatim prefix of `source`
  // (typically empty or an XML declaration).
  uint32_t inject_point = 0;
  uint32_t open_len = 0;
  uint32_t source_size = 0;

  bool active() const { return open_len > 0; }
};

bool uses_xml_fragment_wrap(TreeSitterLangKind lang);

// Find where to inject the synthetic root (0, or right after a leading XMLDecl).
uint32_t xml_fragment_inject_point(const std::string& source);

XmlFragmentWrap xml_wrap_fragment_source(const std::string& source);

// Map a byte offset in the wrapped buffer back to the original source, or npos.
std::size_t xml_wrapped_byte_to_original(const XmlFragmentWrap& wrap, uint32_t wrapped_byte);

// Inverse: original byte → wrapped byte.
uint32_t xml_original_byte_to_wrapped(const XmlFragmentWrap& wrap, uint32_t original_byte);

void xml_unmap_highlights_from_wrap(std::vector<LineHighlights>* highlights,
                                    const XmlFragmentWrap& wrap, const std::string& source);
void xml_unmap_line_highlights_from_wrap(LineHighlights* highlights, int line_0,
                                         const XmlFragmentWrap& wrap, const std::string& source);

}  // namespace tuide
