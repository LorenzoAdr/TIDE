#pragma once

#include <string>
#include <vector>

#include "ai/repo_map.hpp"
#include "symbols/symbol_kind.hpp"

namespace tuide {

class EmbeddingBackend;
class CodingStemEmbedIndex;

struct GetCodeOfRequest {
  std::string workspace_root;
  std::string file;    // relative or absolute
  std::string symbol;  // optional bare name
  int line = 0;        // 1-based hint; 0 = unknown
  int max_lines = 120;
};

struct GetCodeOfResult {
  bool ok = false;
  std::string path;
  std::string name;
  SymbolKind kind = SymbolKind::kFunction;
  int start_line = 0;
  int end_line = 0;
  std::string text;
  bool truncated = false;
  std::string error;
};

// Extract a class/function/file body via sync tree-sitter (not the LLM).
GetCodeOfResult get_code_of(const GetCodeOfRequest& req);

// Parse tool arg forms: "path:Symbol", "path:42", "path", "Symbol".
GetCodeOfRequest parse_get_code_of_arg(const std::string& arg, const std::string& workspace_root);

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
