#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/workspace_model.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"

namespace tuide {

struct ContextPackFragment {
  std::string path;
  int start_line = 1;
  int end_line = 1;
  std::string kind;  // selection, hit_body, caller, header, diagnostic
  std::string text;
};

struct ContextPack {
  std::vector<std::string> seeds;
  std::string seed_note;
  std::vector<ContextPackFragment> fragments;
  std::string dump_text() const;
};

struct ContextPackOptions {
  std::vector<std::string> seeds;
  int max_hits_per_seed = 8;
  int max_fragments = 24;
  int max_body_lines = 80;
  bool include_incoming_calls = true;
  bool include_headers = true;
};

class ContextPackAssembler {
 public:
  ContextPackAssembler(WorkspaceModel* workspace, WorkspaceIndexer* indexer,
                       std::shared_ptr<ISymbolProvider> symbols);

  ContextPack assemble(const ContextPackOptions& opts) const;

 private:
  WorkspaceModel* workspace_ = nullptr;
  WorkspaceIndexer* indexer_ = nullptr;
  std::shared_ptr<ISymbolProvider> symbols_;
};

}  // namespace tuide
