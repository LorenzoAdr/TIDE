#include "l1_intent_debug_cmd.hpp"

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
#include "ai/l2_brain.hpp"
#include "ai/level1_agent.hpp"
#include "ai/llama_backend.hpp"
#include "ai/model_store.hpp"
#include "app/workspace_config.hpp"
#include "app/workspace_model.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/tree_sitter_symbol_provider.hpp"

namespace fs = std::filesystem;

namespace tuide {
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
  std::cerr << "Usage: tuide l1-debug --query \"...\" [--workspace ROOT] [--no-stem-embed] "
               "[--l2-distill] [--map-out PATH] [--seeds-out PATH]\n"
               "  --no-stem-embed  skip coding-stem index; still starts embed for map rerank\n";
}

void write_seeds_json(const fs::path& path, const std::vector<std::string>& seeds) {
  std::ostringstream sj;
  sj << '[';
  for (std::size_t i = 0; i < seeds.size(); ++i) {
    if (i) {
      sj << ',';
    }
    sj << '"';
    for (char c : seeds[i]) {
      if (c == '"' || c == '\\') {
        sj << '\\';
      }
      sj << c;
    }
    sj << '"';
  }
  sj << ']';
  std::ofstream out(path);
  out << sj.str() << '\n';
}

}  // namespace

int run_l1_intent_debug_cli(int argc, char** argv) {
  std::string workspace = fs::current_path().string();
  std::string query;
  std::string map_out_path;
  std::string seeds_out_path;
  bool no_stem_embed = false;
  bool l2_distill = false;
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
    } else if (a == "--no-stem-embed") {
      no_stem_embed = true;
    } else if (a == "--l2-distill") {
      l2_distill = true;
    } else if (a == "--map-out") {
      map_out_path = need("--map-out");
    } else if (a == "--seeds-out") {
      seeds_out_path = need("--seeds-out");
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

  // Workspace AI settings (embed n_ctx, ports, models) so battery matches IDE.
  AiSettings settings = WorkspaceConfig::load(workspace).ai;
  if (settings.models_cache_dir.empty()) {
    settings.models_cache_dir = ModelStore::default_cache_dir();
  }
  settings.level2_workflow = "plan";
  settings.level1.max_steps = 1;
  settings.level1.temperature = 0.1f;

  WorkspaceIndexer file_indexer;
  auto symbols = std::make_shared<TreeSitterSymbolProvider>();
  SymbolWorkspaceIndexer symbol_indexer;
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

  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  // Always warm embeddings for map body/signature rerank. --no-stem-embed only
  // skips the coding-stem index build (expensive), not the embed server.
  auto embed_backend = std::make_unique<EmbeddingBackend>();
  if (!embed_backend->ensure_ready(settings, progress, &err)) {
    std::cerr << "embed ensure_ready: " << err << '\n';
    return 1;
  }
  CodingStemEmbedIndex stem_index;
  if (!no_stem_embed) {
    if (!stem_index.ensure(snap.get(), embed_backend.get(), settings.models_cache_dir,
                           settings.level0.embeddings.model_id, progress, &err,
                           default_stem_passage_profile())) {
      std::cerr << "stem_index ensure: " << err << '\n';
      return 1;
    }
  }

  LlamaBackend backend;
  if (!backend.ensure_ready(settings, progress, &err)) {
    std::cerr << "llama ensure_ready: " << err << '\n';
    return 1;
  }

  LlamaBackend l2_backend;
  std::string l2_err;
  LocalL2Brain l2_warm(&l2_backend);
  const bool l2_ready = l2_warm.ensure_ready(settings, progress, &l2_err);

  // Modo --l2-distill: una sola llamada directa al 7B (sin pipeline L1 completo).
  if (l2_distill) {
    if (!l2_ready) {
      std::cerr << "l2 ensure_ready: " << l2_err << '\n';
      return 1;
    }
    std::cout << "=== L2-distill mode ===\n";
    std::cout << "query: " << query << "\n";
    LlamaCompletionRequest req;
    req.system_prompt =
        "Eres un experto en recuperación de código. Dado un prompt de usuario en lenguaje "
        "natural, analiza la intención real y genera seeds de búsqueda para localizar el "
        "código relevante en la base de código.\n"
        "Responde SOLO con JSON válido, sin markdown ni prosa. Formato exacto:\n"
        "{\"intent\":\"<frase corta en inglés técnico>\","
        "\"primary_goal\":\"<meta principal abstracta>\","
        "\"facets\":[\"<concepto_impl_1>\",\"<concepto_impl_2>\"],"
        "\"ignore\":[\"<término_ruido>\"],"
        "\"search_terms\":[\"<term1>\",\"<term2>\",\"<term3>\"],"
        "\"seeds\":[\"<SpecificIdentifier>\",\"<specific_function>\",\"<ClassName>\"]}\n"
        "Reglas de intención:\n"
        "- Clasifica en: persistencia/estado, navegación, renderizado, runtime, "
        "integración externa, edición, búsqueda o UI.\n"
        "- PRIORIZA lo estructural sobre lo cosmético: estado, modelo, coordinación, "
        "flujo, ciclo de vida.\n"
        "- facets/search_terms: conceptos de IMPLEMENTACION, no palabras de presentación "
        "(visible/panel/ventana/pestaña) salvo que formen parte de un concepto más profundo.\n"
        "Reglas de seeds:\n"
        "- 8..16 identificadores específicos (archivo/clase/función), preferiblemente compuestos.\n"
        "- Vocabulario típico de código en inglés (snake_case, CamelCase).\n"
        "- Si la query combina palabras UI (modal/tab/panel/dialog), los seeds deben ser "
        "compuestos (p. ej. SettingsModal, session_state, workspace_model).\n"
        "- cierre/salir → quit/close/exit/shutdown. compilar → compile/build/cmake.\n"
        "- PROHIBIDO seeds genéricos: Modal, panel, dialog, manager, file, tab (sin cualificador).\n"
        "Ejemplo de respuesta correcta para 'restaurar los ficheros abiertos al reiniciar':\n"
        "{\"intent\":\"session persistence on startup\","
        "\"primary_goal\":\"restore open files and editor state from previous session\","
        "\"facets\":[\"session_state\",\"workspace_persistence\",\"file_restore\"],"
        "\"ignore\":[\"panel\",\"visible\"],"
        "\"search_terms\":[\"session\",\"restore\",\"persist\",\"startup\",\"workspace\"],"
        "\"seeds\":[\"SessionState\",\"workspace_model\",\"restore_session\","
        "\"open_files_state\",\"EditorSessionStore\",\"session_manager\","
        "\"persist_workspace\",\"load_session\"]}\n";
    req.user_prompt = "Consulta:\n" + query + "\n\nJSON:";
    req.max_tokens = 600;
    req.n_ctx = 2048;
    req.temperature = 0.1;
    req.context_role = "L2";
    const auto t0 = std::chrono::steady_clock::now();
    const auto completion = l2_backend.complete(req, nullptr);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    std::cout << "elapsed_ms=" << ms << "\n";
    std::cout << "ok=" << (completion.ok ? 1 : 0) << "\n";
    if (completion.ok) {
      std::cout << "--- raw response ---\n" << completion.text << "\n";
    } else {
      std::cout << "error=" << completion.error << "\n";
    }
    return completion.ok ? 0 : 1;
  }

  WorkspaceModel workspace_model;
  workspace_model.root = workspace;

  Level1Agent agent({.tools = nullptr,
                     .tasks = nullptr,
                     .workspace = &workspace_model,
                     .symbol_indexer = &symbol_indexer,
                     .backend = &backend,
                     .l2_backend = l2_ready ? &l2_backend : nullptr,
                     .embed = embed_backend.get(),
                     .coding_stem_index = no_stem_embed ? nullptr : &stem_index,
                     .settings = settings});

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
  std::cout << "\nsemantic_tokens:";
  for (const auto& t : result.semantic_tokens) {
    std::cout << ' ' << t;
  }
  std::cout << "\n";

  const fs::path map_path = fs::path(workspace) / ".tuide" / "ai" / "map_last.md";
  if (!seeds_out_path.empty()) {
    write_seeds_json(seeds_out_path, result.seeds);
    std::cout << "seeds_out=" << seeds_out_path << " n=" << result.seeds.size() << "\n";
    const fs::path sem_path =
        fs::path(seeds_out_path).parent_path() / "semantic_tokens.json";
    write_seeds_json(sem_path.string(), result.semantic_tokens);
    std::cout << "semantic_tokens_out=" << sem_path << " n=" << result.semantic_tokens.size()
              << "\n";
  }
  if (!map_out_path.empty()) {
    std::error_code ec;
    fs::create_directories(fs::path(map_out_path).parent_path(), ec);
    fs::copy_file(map_path, map_out_path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      std::cerr << "map_out copy failed: " << ec.message() << "\n";
      return 1;
    }
    std::cout << "map_out=" << map_out_path << "\n";
  }
  const std::string map = read_file(map_path);
  if (!map.empty() && map_out_path.empty()) {
    std::cout << "=== map head ===\n";
    std::istringstream in(map);
    std::string line;
    for (int i = 0; i < 40 && std::getline(in, line); ++i) {
      std::cout << line << '\n';
    }
  }
  return result.ok ? 0 : 1;
}

}  // namespace tuide

