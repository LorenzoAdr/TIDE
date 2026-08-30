#pragma once

#include <string>
#include <vector>

#include "ai/repo_map.hpp"
#include "symbols/symbol_kind.hpp"

namespace tuide {

class EmbeddingBackend;
class CodingStemEmbedIndex;

// How to pick lines when the symbol/file span exceeds max_lines.
enum class GetCodeOfWindow {
  Auto,   // head + tail (default; best for Search/Replace)
  Head,   // first max_lines
  Mid,    // middle window
  Tail,   // last max_lines
  Range,  // explicit range_start..range_end (file lines, 1-based)
};

struct GetCodeOfRequest {
  std::string workspace_root;
  std::string file;    // relative or absolute
  std::string symbol;  // optional bare name
  int line = 0;        // 1-based hint; 0 = unknown
  int max_lines = 120;
  GetCodeOfWindow window = GetCodeOfWindow::Auto;
  int range_start = 0;  // Range window (inclusive)
  int range_end = 0;
};

struct GetCodeOfResult {
  bool ok = false;
  std::string path;
  std::string name;
  SymbolKind kind = SymbolKind::kFunction;
  // Full symbol (or file) span when known.
  int symbol_start = 0;
  int symbol_end = 0;
  // Lines actually included in `text` (for a single contiguous window).
  // For Auto head+tail, sent_* cover the union; see omitted_* and text marker.
  int start_line = 0;
  int end_line = 0;
  int sent_start = 0;
  int sent_end = 0;
  int omitted_start = 0;  // Auto head+tail gap; 0 = none
  int omitted_end = 0;
  std::string text;
  bool truncated = false;
  std::string refetch_hint;  // actionable get_code_of arg (prefer relative path)
  std::string error;
};

// Extract a class/function/file body via sync tree-sitter (not the LLM).
GetCodeOfResult get_code_of(const GetCodeOfRequest& req);

// Parse tool arg forms:
//   path:Symbol | path:line | path:start-end | path:Symbol#head|mid|tail | Symbol
GetCodeOfRequest parse_get_code_of_arg(const std::string& arg, const std::string& workspace_root);

// Markdown fence language for a path or get_code_of target (path:Symbol, path:A-B, #tail).
// Empty if unknown — opener is then a bare ``` (same as dataflow snippets).
inline std::string fence_lang_for_path(const std::string& path) {
  std::string p = path;
  const auto hash = p.find('#');
  if (hash != std::string::npos) {
    p.resize(hash);
  }
  const auto slash = p.find_last_of("/\\");
  auto colon = p.rfind(':');
  while (colon != std::string::npos && (slash == std::string::npos || colon > slash)) {
    p.resize(colon);
    colon = p.rfind(':');
  }
  if (p.find("CMakeLists") != std::string::npos) {
    return "cmake";
  }
  const auto dot = p.find_last_of('.');
  if (dot == std::string::npos) {
    return {};
  }
  const std::string ext = p.substr(dot + 1);
  if (ext == "hpp" || ext == "h" || ext == "hh" || ext == "cpp" || ext == "cc" || ext == "cxx" ||
      ext == "c") {
    return "cpp";
  }
  if (ext == "py") {
    return "python";
  }
  if (ext == "rs") {
    return "rust";
  }
  if (ext == "go") {
    return "go";
  }
  if (ext == "sh" || ext == "bash") {
    return "bash";
  }
  if (ext == "js" || ext == "mjs" || ext == "cjs") {
    return "javascript";
  }
  if (ext == "ts" || ext == "tsx") {
    return "typescript";
  }
  if (ext == "cmake") {
    return "cmake";
  }
  return {};
}

// ```lang\ntext\n```  (closer always on its own line).
inline std::string wrap_source_fence(const std::string& text, const std::string& path) {
  std::string out = "```";
  out += fence_lang_for_path(path);
  out += '\n';
  out += text;
  if (!text.empty() && text.back() != '\n') {
    out += '\n';
  }
  out += "```\n";
  return out;
}

// Span / [TRUNCATED] / refetch lines — no source body (so a fence can wrap got.text).
std::string format_get_code_of_header(const GetCodeOfResult& got,
                                      const std::string& display_path = {});

// Tool / pack text: header with [TRUNCATED] metadata + body (unfenced).
// display_path: prefer workspace-relative for refetch hints.
std::string format_get_code_of_result(const GetCodeOfResult& got,
                                      const std::string& display_path = {});

// Write a coding-oriented context pack to <workspace>/.tuide/ai/context_last.md:
// Outline (stem firmas) + Bodies (anchors + actionable funcs). Optional snapshot enriches
// the dominant stem before ranking.
// Returns abs path or {}.
std::string dump_context_last_md(const std::string& workspace_root, const std::string& query,
                                 const RepoMap& map, std::size_t max_n = 14,
                                 std::string* err_out = nullptr,
                                 const SymbolIndexSnapshot* snapshot = nullptr,
                                 EmbeddingBackend* embed = nullptr,
                                 CodingStemEmbedIndex* stem_index = nullptr);

struct RankedMapDumpOptions {
  std::string workspace_root;
  std::string query;
  std::string note;
  std::vector<RepoMapEntry> entries;
  // Optional parallel body texts (same size as entries). Used when include_bodies.
  std::vector<std::string> body_texts;
  bool include_bodies = false;
  std::size_t max_entries = 16;
  std::size_t max_bodies = 8;
  // Filename under .tuide/ai/ (default map_last.md). Also mirrored to context_last.md.
  std::string filename = "map_last.md";
};

// Ranked map for L2 / L1 fallback: query + note + scored entries; optional bodies.
// Writes <workspace>/.tuide/ai/<filename> and mirrors to context_last.md.
std::string dump_ranked_map_md(const RankedMapDumpOptions& opts, std::string* err_out = nullptr);

}  // namespace tuide
