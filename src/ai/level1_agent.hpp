#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "ai/ai_types.hpp"
#include "ai/level1_action.hpp"
#include "ai/llama_backend.hpp"
#include "ai/coding_embed_rerank.hpp"
#include "ai/coding_symbol_embed_index.hpp"
#include "ai/task_runner.hpp"
#include "ai/tool_registry.hpp"
#include "app/workspace_model.hpp"
#include "indexer/symbol_workspace_indexer.hpp"

namespace tuide {

class EmbeddingBackend;
class CodingStemEmbedIndex;

struct Level1AgentDeps {
  ToolRegistry* tools = nullptr;
  TaskRunner* tasks = nullptr;
  WorkspaceModel* workspace = nullptr;
  SymbolWorkspaceIndexer* symbol_indexer = nullptr;
  LlamaBackend* backend = nullptr;
  LlamaBackend* l2_backend = nullptr;   // optional; semantic two-pass retrieval
  EmbeddingBackend* embed = nullptr;  // optional; coding-pack semantic rerank
  CodingStemEmbedIndex* coding_stem_index = nullptr;
  CodingSymbolEmbedIndex* coding_symbol_index = nullptr;  // unused; kept for ABI/tests
  AiSettings settings;
  // Notifica cada tool invocada (p.ej. para memoria conversacional L0).
  std::function<void(const std::string& name, const std::string& arg)> on_tool;
};

struct Level1RunResult {
  bool ok = false;
  bool needs_level2 = false;
  std::string final_text;
  std::string instruction;
  std::vector<std::string> seeds;
  std::vector<std::string> semantic_tokens;
  // agent|ask|plan|git — copied from settings at handoff (typed L1→L2).
  std::string workflow = "agent";
  // Full problem_frame_v1 JSON (may include anchor_hypotheses) for L2 bootstrap.
  std::string problem_frame_json;
  std::string error;
};

struct InvestigateNeedlesResult {
  std::vector<std::string> lexical_seeds;
  std::vector<std::string> semantic_tokens;
  // Short distilled primary_goal/intent for hybrid body-embed query (may be empty).
  std::string embed_intent;
  // Full problem_frame_v1 JSON when distill succeeded (may include hyps).
  std::string problem_frame_json;
};

class Level1Agent {
 public:
  using LogFn = std::function<void(const std::string& line)>;

  explicit Level1Agent(Level1AgentDeps deps);

  Level1RunResult run(const std::string& user_message, const LogFn& log,
                      std::atomic<bool>* cancel = nullptr);

 private:
  std::string build_system_prompt() const;
  std::string editor_context_snippet() const;
  std::string invoke_tool_logged(const std::string& name, const std::string& arg,
                                 const LogFn& log);
  std::string run_seeds_pack(const std::vector<std::string>& seeds, const LogFn& log);
  // Ask L1/L2 for compound seeds (lexical) + loose semantic tokens (body embed).
  InvestigateNeedlesResult propose_investigate_needles(
      const std::string& user_message,
      const LogFn& log,
      std::atomic<bool>* cancel,
      const std::vector<std::string>& map_outline = {});

  Level1AgentDeps deps_;
};

}  // namespace tuide
