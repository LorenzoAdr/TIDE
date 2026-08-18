#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/ai_types.hpp"
#include "app/workspace_model.hpp"
#include "git/git_service.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"

namespace tuide {

struct AiToolResult {
  bool ok = true;
  std::string text;
};

using AiToolHandler = std::function<AiToolResult(const std::string& arg)>;

struct AiToolContext {
  WorkspaceModel* workspace = nullptr;
  std::shared_ptr<ISymbolProvider> symbols;
  WorkspaceIndexer* indexer = nullptr;
  SymbolWorkspaceIndexer* symbol_indexer = nullptr;
  GitService* git = nullptr;
  std::string workspace_root;
  // Live accessor so path_scope updates apply without re-registering tools.
  std::function<const std::vector<std::string>&()> path_scope_fn;
};

class ToolRegistry {
 public:
  void register_tool(std::string name, std::string help, AiToolHandler handler);
  bool has(const std::string& name) const;
  AiToolResult invoke(const std::string& name, const std::string& arg) const;
  std::vector<std::pair<std::string, std::string>> list_tools() const;

  static void register_builtin_read_tools(ToolRegistry* registry, AiToolContext ctx);

 private:
  struct Entry {
    std::string help;
    AiToolHandler handler;
  };
  std::unordered_map<std::string, Entry> tools_;
};

}  // namespace tuide
