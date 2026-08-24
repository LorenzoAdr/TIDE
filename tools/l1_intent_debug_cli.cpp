#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "ai/ai_types.hpp"
#include "ai/coding_stem_embed_index.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/level1_agent.hpp"
#include "ai/llama_backend.hpp"
#include "ai/model_store.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/tree_sitter_symbol_provider.hpp"

namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& p) {
  std::ifstream in(p);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

void usage() {
  std::cerr << "Usage: l1_intent_debug_cli --query \"...\" [--workspace ROOT]\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string workspace = fs::current_path().string();
  std::string query;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto need = [&](const char* flag) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "missing " << flag << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--workspace") {
      workspace = need("--workspace");
    } else if (a == "--query") {
      query = need("--query");
    } else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    } else {
      std::cerr << "unknown " << a << "\n";
      return 2;
    }
  }
  if (query.empty()) {
    usage();
    return 2;
  }

  tuide::AiSettings settings;
  settings.models_cache_dir = tuide::ModelStore::default_cache_dir();
  settings.level2_workflow = "plan";
  settings.level1.max_steps = 1;
  settings.level1.temperature = 0.1f;

  tuide::WorkspaceIndexer file_indexer;
  auto symbols = std::make_shared<tuide::TreeSitterSymbolProvider>();
  tuide::SymbolWorkspaceIndexer symbol_indexer;
  file_indexer.start_scan(workspace);
  symbol_indexer.start_scan(workspace, symbols, &file_indexer);
  while (file_indexer.scanning() || symbol_indexer.scanning()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  const auto snap = symbol_indexer.snapshot();
  if (!snap || snap->symbols.empty()) {
    std::cerr << "symbol index empty\n";
    return 1;
  }

  tuide::EmbeddingBackend embed_backend;
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  if (!embed_backend.ensure_ready(settings, progress, &err)) {
    std::cerr << "embed ensure_ready: " << err << '\n';
    return 1;
  }
  tuide::CodingStemEmbedIndex stem_index;
  if (!stem_index.ensure(snap.get(), &embed_backend, settings.models_cache_dir,
                         settings.level0.embeddings.model_id, progress, &err,
                         tuide::default_stem_passage_profile())) {
    std::cerr << "stem_index ensure: " << err << '\n';
    return 1;
  }

  tuide::LlamaBackend backend;
  if (!backend.ensure_ready(settings, progress, &err)) {
    std::cerr << "llama ensure_ready: " << err << '\n';
    return 1;
  }

  tuide::Level1AgentDeps deps;
  deps.tools = nullptr;
  deps.tasks = nullptr;
  deps.workspace = nullptr;
  deps.symbol_indexer = &symbol_indexer;
  deps.backend = &backend;
  deps.embed = &embed_backend;
  deps.coding_stem_index = &stem_index;
  deps.settings = settings;
  tuide::Level1Agent agent(deps);

  std::cout << "=== L1 debug start ===\n";
  std::cout << "query: " << query << "\n";
  const auto result = agent.run(query, [](const std::string& line) { std::cout << line << '\n'; });
  std::cout << "=== L1 debug result ===\n";
  std::cout << "ok=" << (result.ok ? 1 : 0) << " needs_level2=" << (result.needs_level2 ? 1 : 0)
            << " workflow=" << result.workflow << "\n";
  if (!result.error.empty()) {
    std::cout << "error=" << result.error << "\n";
  }
  if (!result.instruction.empty()) {
    std::cout << "instruction=" << result.instruction << "\n";
  }
  std::cout << "seeds:";
  for (const auto& s : result.seeds) {
    std::cout << ' ' << s;
  }
  std::cout << "\n";

  const fs::path map_path = fs::path(workspace) / ".tuide" / "ai" / "map_last.md";
  const std::string map = read_file(map_path);
  if (!map.empty()) {
    std::cout << "=== map head ===\n";
    std::istringstream in(map);
    std::string line;
    for (int i = 0; i < 40 && std::getline(in, line); ++i) {
      std::cout << line << '\n';
    }
  }
  return result.ok ? 0 : 1;
}
