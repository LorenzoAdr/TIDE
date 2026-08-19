#include "ai/level1_agent.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ai/ai_trace.hpp"
#include "ai/coding_embed_rerank.hpp"
#include "ai/coding_symbol_embed_index.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/get_code_of.hpp"
#include "ai/level1_action.hpp"
#include "ai/repo_map.hpp"
#include "ai/search_needles.hpp"
#include "indexer/symbol_workspace_indexer.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace tuide {
namespace {

std::string relative_active_file(const WorkspaceModel* workspace) {
  if (workspace == nullptr || workspace->buffer.path.empty()) {
    return {};
  }
  if (workspace->root.empty()) {
    return workspace->buffer.path;
  }
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path rel = fs::relative(fs::path(workspace->buffer.path), fs::path(workspace->root), ec);
  if (ec || rel.empty() || rel.native().rfind("..", 0) == 0) {
    return workspace->buffer.path;
  }
  return rel.generic_string();
}

RepoMap build_repo_map_from_indexer(SymbolWorkspaceIndexer* indexer, const RepoMapOptions& opts) {
  if (indexer == nullptr) {
    RepoMap map;
    map.note = "sin SymbolWorkspaceIndexer";
    return map;
  }
  const auto snap = indexer->snapshot();
  return build_repo_map(snap.get(), opts);
}

}  // namespace

namespace {

const char* action_kind_name(Level1ActionKind kind) {
  switch (kind) {
    case Level1ActionKind::Tool:
      return "tool";
    case Level1ActionKind::Seeds:
      return "seeds";
    case Level1ActionKind::Final:
      return "final";
    case Level1ActionKind::NeedsLevel2:
      return "needs_level2";
    case Level1ActionKind::Error:
      return "error";
    case Level1ActionKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

struct DistilledInvestigateIntent {
  std::string intent;
  std::string primary_goal;
  std::vector<std::string> facets;
  std::vector<std::string> ignore;
  std::vector<std::string> search_terms;
};

bool is_surface_presentation_term(const std::string& raw);

void filter_surface_terms(std::vector<std::string>* items) {
  if (items == nullptr) {
    return;
  }
  std::vector<std::string> kept;
  kept.reserve(items->size());
  for (const auto& s : *items) {
    if (!is_surface_presentation_term(s)) {
      kept.push_back(s);
    }
  }
  items->swap(kept);
}

std::string semantic_query_from_distilled(const DistilledInvestigateIntent& di,
                                          const std::string& fallback) {
  std::ostringstream out;
  if (!di.primary_goal.empty()) {
    out << di.primary_goal;
  } else if (!di.intent.empty()) {
    out << di.intent;
  }
  for (const auto& f : di.facets) {
    if (out.tellp() > 0) {
      out << ' ';
    }
    out << f;
  }
  for (const auto& t : di.search_terms) {
    if (out.tellp() > 0) {
      out << ' ';
    }
    out << t;
  }
  return out.tellp() > 0 ? out.str() : fallback;
}

std::vector<std::string> outline_from_ranked_map(const RepoMap& map, std::size_t max_n = 32) {
  std::vector<std::string> outline;
  std::unordered_set<std::string> seen_stems;
  for (const auto& e : map.entries) {
    if (outline.size() >= max_n) {
      break;
    }
    std::string stem = e.stem;
    if (stem.empty()) {
      const auto slash = e.file.find_last_of("/\\");
      const std::string base = slash == std::string::npos ? e.file : e.file.substr(slash + 1);
      const auto dot = base.find_last_of('.');
      stem = dot != std::string::npos ? base.substr(0, dot) : base;
    }
    if (!stem.empty() && seen_stems.insert(stem).second) {
      outline.push_back(stem + "  " + e.file + "  " + e.name);
    }
  }
  return outline;
}

std::optional<DistilledInvestigateIntent> parse_distilled_intent_json(const std::string& raw) {
  try {
    const auto doc = nlohmann::json::parse(raw);
    if (!doc.is_object()) {
      return std::nullopt;
    }
    DistilledInvestigateIntent out;
    out.intent = doc.value("intent", "");
    out.primary_goal = doc.value("primary_goal", "");
    auto read_arr = [](const nlohmann::json& j, const char* key, std::vector<std::string>* dst) {
      if (!j.contains(key) || !j[key].is_array() || dst == nullptr) {
        return;
      }
      for (const auto& it : j[key]) {
        if (it.is_string()) {
          dst->push_back(it.get<std::string>());
        }
      }
    };
    read_arr(doc, "facets", &out.facets);
    read_arr(doc, "ignore", &out.ignore);
    read_arr(doc, "search_terms", &out.search_terms);
    if (out.intent.empty() && out.primary_goal.empty() && out.facets.empty() &&
        out.search_terms.empty()) {
      return std::nullopt;
    }
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

std::string summarize_distilled_intent(const DistilledInvestigateIntent& di) {
  std::ostringstream out;
  if (!di.intent.empty()) {
    out << "intent=" << di.intent;
  }
  if (!di.primary_goal.empty()) {
    if (out.tellp() > 0) {
      out << " | ";
    }
    out << "goal=" << di.primary_goal;
  }
  if (!di.facets.empty()) {
    if (out.tellp() > 0) {
      out << " | ";
    }
    out << "facets:";
    for (std::size_t i = 0; i < di.facets.size() && i < 5; ++i) {
      out << (i == 0 ? ' ' : ',') << di.facets[i];
    }
  }
  if (!di.search_terms.empty()) {
    if (out.tellp() > 0) {
      out << " | ";
    }
    out << "terms:";
    for (std::size_t i = 0; i < di.search_terms.size() && i < 5; ++i) {
      out << (i == 0 ? ' ' : ',') << di.search_terms[i];
    }
  }
  return out.str();
}

bool is_surface_presentation_term(const std::string& raw) {
  std::string low = raw;
  for (char& c : low) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  static const std::unordered_set<std::string> kPresentationWords = {
      "visible", "visibility", "show",     "shown",   "hide",   "hidden",
      "open",    "opened",     "close",    "closed",  "panel",  "panels",
      "window",  "windows",    "dialog",   "dialogs", "screen", "screens",
      "sidebar", "sidebars",   "view",     "views",   "tab",    "tabs",
      "line",    "lines",      "ui",       "ux",
  };
  static const std::unordered_set<std::string> kSemanticAnchors = {
      "state",      "status",     "persist",   "persistent", "storage",  "store",
      "config",     "settings",   "session",   "history",    "cache",    "snapshot",
      "restore",    "reload",     "reopen",    "resume",     "serialize","deserialize",
      "model",      "controller", "manager",   "registry",   "runtime",  "pipeline",
      "document",   "buffer",     "editor",    "workspace",  "project",  "context",
      "navigation", "selection",  "cursor",    "position",   "layout",   "metadata",
  };
  if (kSemanticAnchors.count(low) != 0) {
    return false;
  }
  if (kPresentationWords.count(low) != 0) {
    return true;
  }
  // If a phrase is mostly about presentation, keep it only when it is tied to
  // a stronger semantic anchor like state, persistence, workflow or ownership.
  const bool has_presentation = low.find("visible") != std::string::npos ||
                                low.find("show") != std::string::npos ||
                                low.find("hide") != std::string::npos ||
                                low.find("panel") != std::string::npos ||
                                low.find("window") != std::string::npos ||
                                low.find("dialog") != std::string::npos ||
                                low.find("sidebar") != std::string::npos ||
                                low.find("screen") != std::string::npos ||
                                low.find("tab") != std::string::npos;
  const bool has_anchor = low.find("state") != std::string::npos ||
                          low.find("persist") != std::string::npos ||
                          low.find("store") != std::string::npos ||
                          low.find("config") != std::string::npos ||
                          low.find("session") != std::string::npos ||
                          low.find("history") != std::string::npos ||
                          low.find("cache") != std::string::npos ||
                          low.find("restore") != std::string::npos ||
                          low.find("resume") != std::string::npos ||
                          low.find("manager") != std::string::npos ||
                          low.find("controller") != std::string::npos ||
                          low.find("document") != std::string::npos ||
                          low.find("buffer") != std::string::npos ||
                          low.find("editor") != std::string::npos ||
                          low.find("workflow") != std::string::npos ||
                          low.find("runtime") != std::string::npos;
  return has_presentation && !has_anchor;
}

}  // namespace

Level1Agent::Level1Agent(Level1AgentDeps deps) : deps_(std::move(deps)) {}

std::string Level1Agent::build_system_prompt() const {
  std::ostringstream out;
  out << "Eres el Nivel 1 (agent) de tuide: orquestas tools del IDE. NO generas parches de "
         "código largos; eso es Nivel 2.\n"
         "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO: markdown, ```, prosa suelta, "
         "copiar el mensaje del usuario, o listas interminables.\n"
         "Formatos:\n"
         "{\"action\":\"tool\",\"name\":\"TOOL\",\"arg\":\"...\"}\n"
         "{\"action\":\"tool\",\"name\":\"search\",\"needles\":[\"a\",\"b\",\"c\"]}\n"
         "{\"action\":\"seeds\",\"seeds\":[\"Foo\",\"Bar\"],\"note\":\"plan breve\"}\n"
         "{\"action\":\"final\",\"text\":\"respuesta al usuario\"}\n"
         "{\"action\":\"needs_level2\",\"instruction\":\"qué debe hacer el coder\","
         "\"seeds\":[\"Simbolo\"]}\n"
         "IMPORTANTE: action SOLO tool|seeds|final|needs_level2. "
         "El nombre de la tool va en \"name\", NUNCA en \"action\".\n"
         "INVESTIGAR / LOCALIZAR CÓDIGO: el runtime pide needles, re-rankea REPO_MAP "
         "(léxico shortlist + embed firmas + embed cuerpos) y entrega el mapa a L2.\n"
         "DAME CONTEXTO / INVESTIGAR: needles + mapa rankeado (embed firmas/cuerpos) en "
         "`.tuide/ai/map_last.md`; L2 elige qué leer. L1 NO vuelca bodies ni sustituye a L2.\n"
         "Para un símbolo concreto: tool get_code_of con arg path:Symbol o path:line.\n"
         "SOLO si el usuario pide estado del repo / archivos modificados / diff / commits / "
         "ramas / pull:\n"
         "  {\"action\":\"tool\",\"name\":\"git_status\",\"arg\":\"\"}\n"
         "  {\"action\":\"tool\",\"name\":\"git_diff\",\"arg\":\"src\"}\n"
         "  {\"action\":\"tool\",\"name\":\"git_log\",\"arg\":\"main 5\"}\n"
         "  {\"action\":\"tool\",\"name\":\"git_show\",\"arg\":\"HEAD~1\"}\n"
         "  {\"action\":\"tool\",\"name\":\"git_branches\",\"arg\":\"\"}\n"
         "  {\"action\":\"tool\",\"name\":\"git_pull\",\"arg\":\"\"}\n"
         "NO uses git_* para localizar implementación en el código fuente.\n"
         "NO uses git_* si el usuario pide AÑADIR/CAMBIAR UI o código (pestaña, tab, panel, "
         "texto fijo, label, feature): el runtime elabora el mapa rankeado completo y escala "
         "a L2 (needs_level2); L2 decide qué leer/editar.\n"
         "Ejemplos needs_level2: \"añade una pestaña…\", \"pon un texto fijo…\", "
         "\"cambia el label del tab…\", \"implementa X en console_panel\".\n"
         "Tras observación útil: action=final con resumen breve.\n"
         "NUNCA repitas la misma tool+arg si ya tienes OBSERVATION.\n"
         "Tools permitidas:\n";
  if (deps_.tools != nullptr) {
    for (const auto& [name, help] : deps_.tools->list_tools()) {
      out << "- " << name << ": " << help << '\n';
    }
  }
  out << "También: {\"action\":\"tool\",\"name\":\"run_task\",\"arg\":\"compile\"} "
         "(o launch).\n"
         "Rewrites profundos: needs_level2.\n"
         "Máximo un JSON action por turno. SOLO JSON.\n";
  return out.str();
}

std::string Level1Agent::editor_context_snippet() const {
  if (deps_.workspace == nullptr || deps_.workspace->buffer.path.empty()) {
    return {};
  }
  const auto& buf = deps_.workspace->buffer;
  std::ostringstream out;
  out << "archivo_activo: " << buf.path << '\n';
  out << "cursor: " << (buf.primary_line() + 1) << ':' << (buf.primary_col() + 1) << '\n';
  const int line = buf.primary_line();
  if (line >= 0 && line < buf.lines.size()) {
    const int from = std::max(0, line - 2);
    const int to = std::min(buf.lines.size() - 1, line + 2);
    out << "contexto_local:\n";
    for (int i = from; i <= to; ++i) {
      out << (i + 1) << ": " << buf.lines[i] << '\n';
    }
  }
  return out.str();
}

std::string Level1Agent::invoke_tool_logged(const std::string& name, const std::string& arg,
                                            const LogFn& log) {
  std::string effective_arg = arg;
  if (name == "search") {
    // Expand so tool_key / logs reflect the full candidate set.
    const auto expanded = expand_search_needles(arg, 12);
    if (!expanded.empty()) {
      effective_arg.clear();
      for (std::size_t i = 0; i < expanded.size(); ++i) {
        if (i) {
          effective_arg.push_back('|');
        }
        effective_arg += expanded[i];
      }
      if (effective_arg != arg && log) {
        log("(needles) " + effective_arg);
      }
    }
  }
  if (deps_.on_tool) {
    deps_.on_tool(name, effective_arg);
  }
  if (name == "run_task") {
    if (deps_.tasks == nullptr) {
      return "TaskRunner no disponible";
    }
    if (log) {
      log("→ task " + effective_arg);
    }
    const std::string root =
        deps_.workspace != nullptr ? deps_.workspace->root : std::string{};
    const auto result = deps_.tasks->run(effective_arg, root, [&](const std::string& line) {
      if (log) {
        log(line);
      }
    });
    if (!result.allowed) {
      return "deny: " + result.deny_reason;
    }
    return "exit_code=" + std::to_string(result.exit_code) + "\n" + result.stdout_text;
  }
  if (deps_.tools == nullptr || !deps_.tools->has(name)) {
    return "tool desconocida: " + name;
  }
  if (log) {
    log("→ tool " + name + (effective_arg.empty() ? "" : (" " + effective_arg)));
  }
  const AiToolResult result = deps_.tools->invoke(name, effective_arg);
  if (!result.ok) {
    return "error: " + result.text;
  }
  constexpr std::size_t kMax = 6000;
  if (result.text.size() > kMax) {
    return result.text.substr(0, kMax) + "\n…(truncated)";
  }
  return result.text;
}

std::string Level1Agent::run_seeds_pack(const std::vector<std::string>& seeds, const LogFn& log) {
  std::string arg;
  for (std::size_t i = 0; i < seeds.size(); ++i) {
    if (i) {
      arg.push_back(' ');
    }
    arg += seeds[i];
    if (log) {
      log("seed: `" + seeds[i] + "`");
    }
  }
  return invoke_tool_logged("context_pack", arg, log);
}

namespace {

std::string ascii_lower_simple(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool is_generic_needle_token(const std::string& raw) {
  const std::string s = ascii_lower_simple(raw);
  static const std::unordered_set<std::string> kGeneric = {
      "modal", "dialog", "window", "panel", "process", "button", "manager", "view",
      "form",  "widget", "action", "handler", "helper", "utils",  "util",   "data",
      "item",  "list",   "tree",   "file",   "code",   "main",   "app",    "menu",
      "popup", "page",   "tab",    "bar",    "strip",  "state",  "config", "settings",
      "launch","attach", "run",    "start",  "stop",   "open",   "close",  "create",
  };
  // Bare generics are too broad ("Modal", "process"). Keep compounds / CamelCase multi-part.
  if (s.size() >= 10) {
    return false;
  }
  if (s.find('_') != std::string::npos) {
    return false;
  }
  int upper = 0;
  for (char c : raw) {
    if (std::isupper(static_cast<unsigned char>(c))) {
      ++upper;
    }
  }
  if (upper >= 2) {
    return false;  // DebugLaunchModal-style
  }
  return kGeneric.count(s) > 0;
}

// Rank file stems / symbol names from the index against the NL query (project-agnostic).
std::vector<std::string> rank_index_needle_candidates(SymbolWorkspaceIndexer* indexer,
                                                     const std::string& user_message,
                                                     std::size_t max_n) {
  std::vector<std::string> out;
  if (indexer == nullptr || max_n == 0) {
    return out;
  }
  const auto snap = indexer->snapshot();
  if (snap == nullptr || snap->symbols.empty()) {
    return out;
  }
  const auto qtoks_raw = repo_map_query_tokens(user_message, 24);
  const auto qtoks = expand_nl_retrieval_tokens(qtoks_raw, 48);
  // Prefer matches on specific expanded tokens over the ultra-common "modal" alone.
  auto token_weight = [](const std::string& t) {
    if (t == "modal" || t == "dialog" || t == "window" || t == "panel") {
      return 1;
    }
    if (t.size() >= 7) {
      return 5;
    }
    if (t.size() >= 5) {
      return 3;
    }
    return 2;
  };
  std::unordered_map<std::string, int> score;
  auto consider = [&](std::string id) {
    if (id.size() < 4) {
      return;
    }
    // Drop path prefixes; keep basename stem.
    const auto slash = id.find_last_of("/\\");
    if (slash != std::string::npos) {
      id = id.substr(slash + 1);
    }
    const auto dot = id.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
      id = id.substr(0, dot);
    }
    if (id.size() < 4) {
      return;
    }
    const std::string low = ascii_lower_simple(id);
    if (is_generic_needle_token(id)) {
      return;
    }
    int sc = 0;
    int distinct = 0;
    for (const auto& t : qtoks) {
      if (t.size() < 3) {
        continue;
      }
      const int w = token_weight(t);
      if (low == t) {
        sc += (100 + static_cast<int>(t.size()) * 4) * w;
        ++distinct;
      } else if (low.find(t) != std::string::npos) {
        sc += (40 + static_cast<int>(t.size()) * 3) * w;
        ++distinct;
      } else if (t.find(low) != std::string::npos && low.size() >= 5) {
        sc += 15 * w;
        ++distinct;
      }
    }
    if (sc > 0) {
      // Reward stems that hit several distinct intent tokens (quit+confirm, etc.).
      sc += 35 * std::max(0, distinct - 1);
      sc += std::min(20, static_cast<int>(low.size()));
      score[id] = std::max(score[id], sc);
    }
  };

  for (const auto& sym : snap->symbols) {
    if (sym.file.rfind("third_party/", 0) == 0) {
      continue;
    }
    consider(sym.file);
    if (!sym.name.empty()) {
      consider(sym.name);
    } else if (!sym.display_name.empty()) {
      consider(sym.display_name);
    }
  }

  std::vector<std::pair<int, std::string>> ranked;
  ranked.reserve(score.size());
  for (const auto& [id, sc] : score) {
    ranked.push_back({sc, id});
  }
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) {
      return a.first > b.first;
    }
    return a.second < b.second;
  });
  for (const auto& [sc, id] : ranked) {
    (void)sc;
    out.push_back(id);
    if (out.size() >= max_n) {
      break;
    }
  }
  return out;
}

}  // namespace

std::vector<std::string> Level1Agent::propose_investigate_needles(const std::string& user_message,
                                                                 const LogFn& log,
                                                                 std::atomic<bool>* cancel,
                                                                 const std::vector<std::string>& map_outline) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  auto push = [&](std::string s, bool allow_generic) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
      s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
      s.pop_back();
    }
    if (s.size() < 3) {
      return;
    }
    if (!allow_generic && is_generic_needle_token(s)) {
      return;
    }
    if (!seen.insert(s).second) {
      return;
    }
    out.push_back(std::move(s));
  };

  const std::vector<std::string> index_candidates =
      rank_index_needle_candidates(deps_.symbol_indexer, user_message, 48);
  if (log && !index_candidates.empty()) {
    std::ostringstream cs;
    cs << "candidatos índice:";
    for (std::size_t i = 0; i < index_candidates.size() && i < 24; ++i) {
      cs << ' ' << index_candidates[i];
    }
    if (index_candidates.size() > 24) {
      cs << " …(+" << (index_candidates.size() - 24) << ")";
    }
    log(cs.str());
  }

  std::optional<DistilledInvestigateIntent> distilled;
  std::vector<std::string> semantic_outline = map_outline;
  std::vector<std::string> semantic_index_candidates = index_candidates;
  LlamaBackend* reasoning_backend = nullptr;
  if (deps_.l2_backend != nullptr && deps_.l2_backend->ready()) {
    reasoning_backend = deps_.l2_backend;
  } else if (deps_.backend != nullptr && deps_.backend->ready()) {
    reasoning_backend = deps_.backend;
  }
  if (reasoning_backend != nullptr) {
    {
      LlamaCompletionRequest req;
      req.system_prompt =
          "Eres un analizador semántico para recuperación de código. Tu trabajo en esta primera "
          "pasada es entender la intención real del prompt y separar señal de ruido. "
          "NO elijas todavía archivos ni símbolos concretos. Responde SOLO JSON con este formato:\n"
          "{\"intent\":\"...\",\"primary_goal\":\"...\",\"facets\":[\"...\"],"
          "\"ignore\":[\"...\"],\"search_terms\":[\"...\"]}\n"
          "Reglas:\n"
          "- intent: una frase corta en inglés técnico.\n"
          "- primary_goal: la meta principal, más abstracta que las palabras literales del prompt.\n"
          "- Antes de escribir facets/search_terms, clasifica mentalmente la petición en una "
          "familia de intención general, por ejemplo: persistencia/estado, navegación, "
          "renderizado, ejecución runtime, integración externa, edición, búsqueda o UI.\n"
          "- facets: 2..5 conceptos nucleares de IMPLEMENTACION, no palabras de superficie.\n"
          "- ignore: 0..6 detalles superficiales que podrían desviar el retrieval.\n"
          "- search_terms: 3..8 términos cortos de implementación.\n"
          "- Si el prompt mezcla conceptos estructurales con detalles de presentación, "
          "PRIORIZA lo estructural: estado, persistencia, modelo, coordinación, flujo, "
          "almacenamiento, propietario del dato o ciclo de vida.\n"
          "- Evita que facets/search_terms queden dominados por palabras de presentación o "
          "visibilidad (abrir/cerrar/mostrar/ocultar/panel/ventana/pestaña/línea) salvo que "
          "formen parte de un concepto de implementación más profundo.\n"
          "- NO inventes nombres de archivos o símbolos todavía.\n"
          "- Si el prompt es largo, prioriza la semántica estable frente a detalles cosméticos.\n";
      std::ostringstream user;
      user << "Consulta del usuario:\n" << user_message << "\n";
      user << "\nJSON:";
      req.user_prompt = user.str();
      req.max_tokens = std::min(320, std::max(160, deps_.settings.level1.max_tokens / 2));
      {
        const int prompt_tok_est =
            static_cast<int>((req.system_prompt.size() + req.user_prompt.size()) / 3 + 32);
        const int needed = prompt_tok_est + req.max_tokens + 256;
        int ctx = 512;
        while (ctx < needed) { ctx *= 2; }
        req.n_ctx = std::min(ctx, 2048);
      }
      req.temperature = 0.1;
      req.context_role = reasoning_backend == deps_.l2_backend ? "L2" : "L1";
      req.n_ctx_setting_hint =
          reasoning_backend == deps_.l2_backend ? "ai.level2.n_ctx" : "ai.level1.n_ctx";
      if (log) {
        log(reasoning_backend == deps_.l2_backend
                ? "L1 investigar → L2 pasada 1: destilación semántica…"
                : "L1 investigar → destilando intención…");
      }
      const auto completion = reasoning_backend->complete(req, cancel);
      if (completion.ok) {
        if (log) {
          log("L1 intent raw: " + completion.text);
        }
        distilled = parse_distilled_intent_json(completion.text);
        if (distilled) {
          filter_surface_terms(&distilled->facets);
          filter_surface_terms(&distilled->search_terms);
        }
        if (distilled && log) {
          log("L1 intent: " + summarize_distilled_intent(*distilled));
        }
      } else if (log) {
        log("✗ L1 intent: " + completion.error);
      }
    }

    std::vector<std::string> semantic_rank_terms;
    if (distilled) {
      for (const auto& t : distilled->search_terms) {
        if (!t.empty()) {
          semantic_rank_terms.push_back(t);
        }
      }
      for (const auto& f : distilled->facets) {
        for (const auto& tok : extract_code_tokens(f, 6)) {
          semantic_rank_terms.push_back(tok);
        }
      }
      for (const auto& tok : extract_code_tokens(distilled->primary_goal, 8)) {
        semantic_rank_terms.push_back(tok);
      }
    }
    if (semantic_rank_terms.empty()) {
      semantic_rank_terms = extract_code_tokens(user_message, 16);
    }

    const std::string semantic_query =
        distilled ? semantic_query_from_distilled(*distilled, user_message) : user_message;
    RepoMapOptions semantic_opts;
    semantic_opts.query = semantic_query;
    semantic_opts.extra_needles = semantic_rank_terms;
    semantic_opts.active_file = relative_active_file(deps_.workspace);
    semantic_opts.max_symbols = 96;
    semantic_opts.max_files = 32;
    semantic_opts.max_chars = 4200;
    semantic_opts.max_map_tokens = 1280;
    semantic_opts.path_scope = deps_.settings.path_scope;
    RepoMap semantic_map = build_repo_map_from_indexer(deps_.symbol_indexer, semantic_opts);
    if (!semantic_map.entries.empty()) {
      semantic_outline = outline_from_ranked_map(semantic_map, 32);
      semantic_index_candidates =
          rank_index_needle_candidates(deps_.symbol_indexer, semantic_query, 48);
      if (log) {
        log("REPO_MAP semántico: " + std::to_string(semantic_map.entries.size()) +
            " símbolos (best_score=" + std::to_string(semantic_map.best_score) + ")");
      }
    }

    LlamaCompletionRequest req;
    req.system_prompt =
        "Eres un recuperador de código en segunda pasada. Ya existe un mapa rankeado a partir "
        "de la intención semántica del usuario. Tu trabajo ahora es decidir DÓNDE buscar "
        "en el código y proponer needles/símbolos concretos para recuperar los fragmentos "
        "correctos. Responde SOLO JSON:\n"
        "{\"action\":\"seeds\",\"seeds\":[\"QuitConfirm\",\"quit_confirm\",...]}\n"
        "Reglas:\n"
        "- 8..16 identificadores ESPECÍFICOS (archivo/clase/función), preferible compuestos.\n"
        "- Usa la INTENCION_DESTILADA como fuente principal de verdad.\n"
        "- Usa el MAPA_RANKEADO como ancla principal para decidir por dónde buscar.\n"
        "- Prioriza stems/archivos/símbolos que aparezcan en ese mapa y encajen con la intención.\n"
        "- Traduce la intención a vocabulario típico de código en inglés.\n"
        "- Si la query mezcla UI y semántica estructural, elige símbolos ligados a estado, "
        "persistencia, flujo, modelo o coordinación antes que vocabulario visual.\n"
        "- PROHIBIDO seeds genéricos sueltos: Modal, process, panel, dialog, button, manager, "
        "attach, launch, file, tree, tab, tabs (sin más cualificador).\n"
        "- PROHIBIDO: markdown, tools, prosa, rutas absolutas.\n";
    std::ostringstream user;
    user << "Consulta del usuario:\n" << user_message << "\n";
    if (distilled) {
      user << "\nINTENCION_DESTILADA:\n";
      if (!distilled->intent.empty()) {
        user << "- intent: " << distilled->intent << '\n';
      }
      if (!distilled->primary_goal.empty()) {
        user << "- primary_goal: " << distilled->primary_goal << '\n';
      }
      if (!distilled->facets.empty()) {
        user << "- facets:\n";
        for (const auto& f : distilled->facets) {
          user << "  - " << f << '\n';
        }
      }
      if (!distilled->ignore.empty()) {
        user << "- ignore:\n";
        for (const auto& g : distilled->ignore) {
          user << "  - " << g << '\n';
        }
      }
      if (!distilled->search_terms.empty()) {
        user << "- search_terms:\n";
        for (const auto& t : distilled->search_terms) {
          user << "  - " << t << '\n';
        }
      }
    }
    if (!semantic_index_candidates.empty()) {
      user << "\nCANDIDATOS_DEL_INDICE (ya orientados por la semántica):\n";
      for (const auto& c : semantic_index_candidates) {
        user << "- " << c << '\n';
      }
    }
    if (!semantic_outline.empty()) {
      user << "\nMAPA_RANKEADO (top stems/archivos/símbolos tras la pasada semántica):\n";
      const int show = std::min(static_cast<int>(semantic_outline.size()), 32);
      for (int i = 0; i < show; ++i) {
        user << "- " << semantic_outline[static_cast<std::size_t>(i)] << '\n';
      }
    }
    user << "\nJSON:";
    req.user_prompt = user.str();
    req.max_tokens = std::min(480, std::max(192, deps_.settings.level1.max_tokens));
    {
      const int prompt_tok_est =
          static_cast<int>((req.system_prompt.size() + req.user_prompt.size()) / 3 + 32);
      const int needed = prompt_tok_est + req.max_tokens + 256;
      int ctx = 512;
      while (ctx < needed) { ctx *= 2; }
      req.n_ctx = std::min(ctx, 2048);
    }
    req.temperature = 0.1;
    req.context_role = reasoning_backend == deps_.l2_backend ? "L2" : "L1";
    req.n_ctx_setting_hint =
        reasoning_backend == deps_.l2_backend ? "ai.level2.n_ctx" : "ai.level1.n_ctx";
    if (log) {
      log(reasoning_backend == deps_.l2_backend
              ? "L1 investigar → L2 pasada 2: elegir dónde buscar sobre mapa rankeado…"
              : "L1 investigar → proponiendo needles…");
    }
    const auto needles_t0 = std::chrono::steady_clock::now();
    const auto completion = reasoning_backend->complete(req, cancel);
    const auto needles_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - needles_t0)
                                .count();
    ai_trace(AiTraceChannel::L1, "l1_needles_complete",
             "{\"ok\":" + std::string(completion.ok ? "1" : "0") +
                 ",\"duration_ms\":" + std::to_string(needles_ms) + ",\"reply_chars\":" +
                 std::to_string(completion.text.size()) + "}");
    if (completion.ok) {
      if (log) {
        log("L1 needles raw: " + completion.text);
      }
      ai_trace(AiTraceChannel::L1, "l1_needles_raw",
               "{\"raw\":\"" + ai_trace_escape(completion.text) + "\"}");
      const Level1Action action = parse_level1_action(completion.text);
      for (const auto& s : action.seeds) {
        push(s, false);
      }
      if (action.kind == Level1ActionKind::Tool &&
          (action.tool_name == "search" || action.tool_name == "repo_map")) {
        for (const auto& n : split_search_needles(action.arg)) {
          push(n, false);
        }
      }
    } else if (log) {
      log("✗ L1 needles: " + completion.error);
    }
  }

  // Always merge top index stems so lexical recall is not capped by the LLM alone.
  for (const auto& c : index_candidates) {
    push(c, false);
    if (out.size() >= 20) {
      break;
    }
  }

  for (const auto& t : extract_code_tokens(user_message, 16)) {
    push(t, false);
  }
  if (distilled) {
    for (const auto& t : distilled->search_terms) {
      push(t, false);
    }
    for (const auto& f : distilled->facets) {
      for (const auto& tok : extract_code_tokens(f, 6)) {
        push(tok, false);
      }
    }
  }

  std::vector<std::string> expanded;
  seen.clear();
  for (const auto& n : out) {
    for (const auto& v : expand_identifier_variants(n)) {
      if (v.size() < 3 || is_generic_needle_token(v) || !seen.insert(v).second) {
        continue;
      }
      expanded.push_back(v);
      if (expanded.size() >= 32) {
        break;
      }
    }
    if (expanded.size() >= 32) {
      break;
    }
  }
  // If filtering wiped everything, keep raw index candidates unfiltered-lightly.
  if (expanded.empty()) {
    for (const auto& c : index_candidates) {
      if (c.size() >= 4 && seen.insert(c).second) {
        expanded.push_back(c);
      }
      if (expanded.size() >= 16) {
        break;
      }
    }
  }
  return expanded;
}

Level1RunResult Level1Agent::run(const std::string& user_message, const LogFn& log,
                                 std::atomic<bool>* cancel) {
  Level1RunResult out;
  if (deps_.backend == nullptr) {
    out.error = "sin LlamaBackend";
    return out;
  }
  if (!deps_.backend->ready()) {
    out.error = "backend L1 no listo";
    return out;
  }

  const int max_steps = std::max(1, deps_.settings.level1.max_steps);
  std::string history;
  std::string user = user_message;
  const std::string ctx = editor_context_snippet();
  if (!ctx.empty()) {
    user += "\n\n---\n" + ctx;
  }

  const AiWorkflowKind workflow = parse_ai_workflow_kind(deps_.settings.level2_workflow);
  out.workflow = ai_workflow_kind_name(workflow);

  // Explicit workflow owns the machine: do not let git_repo heuristics block the map pipeline.
  const bool force_l2_workflow = workflow != AiWorkflowKind::Agent;
  const bool context_dump =
      query_asks_context_dump(user_message) &&
      (force_l2_workflow || !query_asks_git_repo(user_message));
  const bool code_edit = workflow == AiWorkflowKind::Agent &&
                         query_asks_code_edit(user_message) && !query_asks_git_repo(user_message);
  // Ask/Plan/Git always use ranked-map explore; Agent keeps previous heuristics.
  const bool code_locate =
      force_l2_workflow ||
      ((query_asks_code_location(user_message) || context_dump || code_edit) &&
       !query_asks_git_repo(user_message));

  RepoMapOptions map_opts;
  map_opts.query = user_message;
  map_opts.active_file = relative_active_file(deps_.workspace);
  // Context dump / code edit: wide catalog — L2 decides from the ranked map.
  // Investigate-only: still broad, but keep embed body cost bounded.
  if (context_dump || code_edit || force_l2_workflow) {
    map_opts.max_symbols = 400;
    map_opts.max_files = 120;
    map_opts.max_chars = 120000;
    map_opts.max_map_tokens = 32000;
  } else {
    map_opts.max_symbols = 256;
    map_opts.max_files = 80;
    map_opts.max_chars = 48000;
    map_opts.max_map_tokens = 12000;
  }
  map_opts.prefer_git_tracked = true;
  map_opts.use_pagerank = true;
  map_opts.path_scope = deps_.settings.path_scope;
  if (deps_.workspace != nullptr) {
    for (const auto& tab : deps_.workspace->tabs) {
      if (tab.path.empty() || tab.git_diff_view) {
        continue;
      }
      std::error_code ec;
      std::string rel = tab.path;
      if (!deps_.workspace->root.empty()) {
        const auto r =
            std::filesystem::relative(std::filesystem::path(tab.path),
                                      std::filesystem::path(deps_.workspace->root), ec);
        if (!ec && !r.empty() && r.native().rfind("..", 0) != 0) {
          rel = r.generic_string();
        }
      }
      if (rel != map_opts.active_file) {
        map_opts.chat_files.push_back(rel);
      }
    }
  }

  auto map_has_query_hits = [](const RepoMap& m) {
    return !m.entries.empty() && m.note.find("query_hits=") != std::string::npos;
  };

  // Investigate / context dump / code edit: needles → REPO_MAP → ranked map → L2 (edit).
  // Lexical-only short-circuit skipped NL like "búsqueda de strings" with weak incidental
  // hits and never showed needles.
  if (code_locate) {
    if (log) {
      const char* via_label =
          code_edit ? "code_edit"
                    : (context_dump ? "context_dump"
                                    : (force_l2_workflow ? ai_workflow_kind_name(workflow)
                                                         : "investigate"));
      log(std::string("L1 agent start (") + via_label +
          "; workflow=" + out.workflow +
          "; max_steps=" + std::to_string(max_steps) + ")");
    }

    const auto lexical_tokens = repo_map_query_tokens(user_message, 24);
    if (log) {
      std::ostringstream ts;
      ts << "tokens léxicos:";
      if (lexical_tokens.empty()) {
        ts << " (ninguno)";
      } else {
        for (const auto& t : lexical_tokens) {
          ts << ' ' << t;
        }
      }
      log(ts.str());
    }

    RepoMap lexical_map = build_repo_map_from_indexer(deps_.symbol_indexer, map_opts);
    if (log) {
      log("REPO_MAP lexical: " + std::to_string(lexical_map.entries.size()) +
          " símbolos (best_score=" + std::to_string(lexical_map.best_score) +
          (map_has_query_hits(lexical_map) ? ", query_hits" : ", sin query_hits") + ")");
    }

    const std::vector<std::string> needles =
        propose_investigate_needles(user_message, log, cancel, [&] {
          // Build a compact outline from the lexical map top entries so the LLM
          // can ground its seed proposals in names that actually exist in the repo.
          std::vector<std::string> outline;
          std::unordered_set<std::string> seen_stems;
          for (const auto& e : lexical_map.entries) {
            if (outline.size() >= 32) {
              break;
            }
            // stem is set by enrich; fall back to file basename without extension.
            std::string stem = e.stem;
            if (stem.empty()) {
              const auto slash = e.file.find_last_of("/\\");
              const std::string base =
                  slash == std::string::npos ? e.file : e.file.substr(slash + 1);
              const auto dot = base.find_last_of('.');
              stem = dot != std::string::npos ? base.substr(0, dot) : base;
            }
            // One line per unique stem: "stem  file  topSymbol"
            if (seen_stems.insert(stem).second) {
              outline.push_back(stem + "  " + e.file + "  " + e.name);
            }
          }
          return outline;
        }());
    out.seeds = needles;
    if (log) {
      log("L1 needles propuestos:");
      if (needles.empty()) {
        log("  (vacío)");
      } else {
        for (const auto& n : needles) {
          log("  • " + n);
        }
      }
    }
    ai_trace(AiTraceChannel::L1, "l1_needles", [&] {
      std::ostringstream j;
      j << "{\"n\":" << needles.size() << ",\"needles\":[";
      for (std::size_t i = 0; i < needles.size(); ++i) {
        if (i) {
          j << ',';
        }
        j << '"' << ai_trace_escape(needles[i]) << '"';
      }
      j << "]}";
      return j.str();
    }());

    auto emit_map_answer = [&](const RepoMap& m, const char* via) {
      const std::string root =
          deps_.workspace != nullptr ? deps_.workspace->root : std::string{};
      const bool l2_active = deps_.settings.level2_mode != "dry_run";

      // Lexical shortlist from the (now wider) map. Embed reorders; do not cut hard —
      // L2 discerns. Context: signatures only + large final list. Investigate: light body pass.
      std::vector<RepoMapEntry> candidates = m.entries;
      if (log) {
        log("L1 lexical shortlist: " + std::to_string(candidates.size()) +
            " candidatos (via " + via + ")");
      }

      TwoStageRerankOptions rr_opts;
      rr_opts.query = user_message;
      rr_opts.needles = needles;
      rr_opts.workspace_root = root;
      if (context_dump || code_edit) {
        // Full ranked map as L2 explore base (signatures only; L2 reads bodies).
        rr_opts.phase_a_pool = 400;
        rr_opts.phase_a_top = 280;
        rr_opts.final_top = 280;
        rr_opts.max_per_file = 14;
        rr_opts.max_per_stem = 3;
        rr_opts.max_per_dir = 20;
        rr_opts.fetch_bodies = false;
        rr_opts.skip_phase_a = false;
        rr_opts.body_max_lines = 80;
      } else {
        rr_opts.phase_a_pool = 256;
        rr_opts.phase_a_top = 96;
        rr_opts.final_top = 120;
        rr_opts.max_per_file = 8;
        rr_opts.max_per_stem = 2;
        rr_opts.max_per_dir = 12;
        rr_opts.fetch_bodies = true;
        rr_opts.skip_phase_a = false;
        rr_opts.body_max_lines = 80;
      }

      if (log) {
        log("L1 two_stage rerank (lexical shortlist → embed firmas/cuerpos): candidatos=" +
            std::to_string(candidates.size()));
      }
      TwoStageRerankResult ranked =
          rerank_map_two_stage(std::move(candidates), rr_opts, deps_.embed);
      if (log) {
        log("L1 embed timing: phase_a_ms=" + std::to_string(ranked.phase_a_ms) +
            " phase_b_ms=" + std::to_string(ranked.phase_b_ms) +
            " total_ms=" + std::to_string(ranked.total_ms) +
            " cand_in=" + std::to_string(ranked.candidates_in) +
            " n=" + std::to_string(ranked.entries.size()) +
            " embed=" + (deps_.embed != nullptr && deps_.embed->ready() ? "1" : "0") +
            " src=lexical_shortlist");
      }
      apply_ranked_map_priors(user_message, &ranked.entries, deps_.coding_stem_index, deps_.embed);
      if (!ranked.note.empty()) {
        ranked.note += "; ";
      }
      ranked.note += "priors=1";

      {
        const SymbolIndexSnapshot* snap_ptr = nullptr;
        std::shared_ptr<const SymbolIndexSnapshot> snap_keep;
        if (deps_.symbol_indexer != nullptr) {
          snap_keep = deps_.symbol_indexer->snapshot();
          snap_ptr = snap_keep.get();
        }
        enrich_ranked_map_hints(&ranked.entries, root, user_message, snap_ptr,
                                deps_.coding_stem_index, deps_.embed, &ranked.body_texts,
                                (context_dump || code_edit) ? 48 : 32);
      }

      ai_trace(AiTraceChannel::L1, "l1_embed_phase_a",
               std::string("{\"used\":") + (ranked.used_phase_a ? "1" : "0") +
                   ",\"ms\":" + std::to_string(ranked.phase_a_ms) +
                   ",\"cand_in\":" + std::to_string(ranked.candidates_in) +
                   ",\"symbol_index\":0}");
      ai_trace(AiTraceChannel::L1, "l1_embed_phase_b",
               std::string("{\"used\":") + (ranked.used_phase_b ? "1" : "0") +
                   ",\"ms\":" + std::to_string(ranked.phase_b_ms) + ",\"bodies\":" +
                   std::to_string(ranked.body_texts.size()) + "}");

      std::string map_note = m.note;
      if (!map_note.empty() && !ranked.note.empty()) {
        map_note += "; ";
      }
      map_note += ranked.note;
      map_note += "; lex_prefilter=1; src=lexical_shortlist";
      if (code_edit) {
        map_note += "; code_edit=1";
      }

      RankedMapDumpOptions dump_opts;
      dump_opts.workspace_root = root;
      dump_opts.query = user_message;
      dump_opts.note = map_note;
      dump_opts.entries = ranked.entries;
      dump_opts.body_texts.clear();
      dump_opts.include_bodies = false;
      dump_opts.max_entries = 0;
      dump_opts.max_bodies = 0;
      dump_opts.filename = "map_last.md";

      std::string err;
      const std::string path = dump_ranked_map_md(dump_opts, &err);
      ai_trace(AiTraceChannel::L1, "l1_ranked_map",
               std::string("{\"via\":\"") + via + "\",\"entries\":" +
                   std::to_string(ranked.entries.size()) + ",\"path\":\"" +
                   ai_trace_escape(path) + "\",\"note\":\"" + ai_trace_escape(map_note) +
                   "\",\"l2\":" + (l2_active ? "1" : "0") +
                   ",\"code_edit\":" + (code_edit ? "1" : "0") +
                   ",\"total_ms\":" + std::to_string(ranked.total_ms) +
                   ",\"symbol_index\":0}");

      out.ok = true;
      std::ostringstream summary;
      if (code_edit) {
        summary << "Cambio de código → mapa rankeado completo para L2";
      } else {
        summary << "Mapa rankeado";
      }
      if (!path.empty()) {
        summary << " → `.tuide/ai/map_last.md`";
      } else if (!err.empty()) {
        summary << " (falló escritura: " << err << ")";
      }
      summary << " (" << ranked.note << ")\n";
      summary << format_ranked_map_answer(ranked.entries,
                                          (context_dump || code_edit) ? 120 : 64, {});
      out.final_text = summary.str();

      // code_edit / explicit workflows always hand off: ranked map is L2's explore base.
      if (l2_active || code_edit || force_l2_workflow) {
        out.needs_level2 = true;
        if (workflow == AiWorkflowKind::Ask) {
          out.instruction =
              "Explora el ## Ranked map, arma un pack con plan/get_code_of y responde con "
              "action=synthesize (explicación clara). No edites. Pedido del usuario:\n" +
              user_message;
        } else if (workflow == AiWorkflowKind::Plan) {
          out.instruction =
              "Explora el ## Ranked map, arma un pack y emite action=synthesize con un plan "
              "de cambios (archivos, pasos, riesgos). No edites ni compiles. Pedido:\n" +
              user_message;
        } else if (workflow == AiWorkflowKind::Git) {
          out.instruction =
              "Usa ## Git context (y el mapa/código si hace falta) para explicar qué cambió. "
              "Emite action=synthesize. No edites. Pedido del usuario:\n" +
              user_message;
        } else if (code_edit) {
          out.instruction =
              "El ## Ranked map es tu base de exploración. Elige candidatos, lee cuerpos "
              "con get_code_of/search/file_outline y decide qué editar con Search/Replace. "
              "Pedido del usuario:\n" +
              user_message;
        } else if (context_dump) {
          out.instruction =
              "Elige del mapa rankeado los símbolos relevantes y lee cuerpos con get_code_of.";
        } else {
          out.instruction =
              "Localiza en el mapa rankeado dónde está la implementación pedida.";
        }
        out.seeds = needles;
        if (out.seeds.empty()) {
          const std::size_t seed_cap =
              (context_dump || code_edit || force_l2_workflow) ? 48 : 24;
          for (const auto& e : ranked.entries) {
            if (!e.name.empty()) {
              out.seeds.push_back(e.name);
            }
            if (out.seeds.size() >= seed_cap) {
              break;
            }
          }
        }
        if (log) {
          log(std::string("L1 → l2_handoff via ") + via + " workflow=" + out.workflow +
              (code_edit ? " (code_edit)" : "") + "; path=" +
              (path.empty() ? "(none)" : path));
          log(out.final_text);
        }
        ai_trace(AiTraceChannel::L1, "l2_handoff",
                 std::string("{\"via\":\"") + via + "\",\"entries\":" +
                     std::to_string(ranked.entries.size()) +
                     ",\"code_edit\":" + (code_edit ? "1" : "0") + ",\"workflow\":\"" +
                     out.workflow + "\"}");
        return;
      }

      if (log) {
        log(std::string("L1 → ranked_map via ") + via + " (" +
            std::to_string(ranked.entries.size()) + " entradas); path=" +
            (path.empty() ? "(none)" : path));
        log(out.final_text);
      }
      ai_trace(AiTraceChannel::L1, "l1_ranked_map_shown",
               std::string("{\"via\":\"") + via + "\",\"entries\":" +
                   std::to_string(ranked.entries.size()) + ",\"context_dump\":" +
                   (context_dump ? "1" : "0") + ",\"code_edit\":" +
                   (code_edit ? "1" : "0") + "}");
    };

    RepoMap repo_map = lexical_map;
    if (!needles.empty()) {
      map_opts.extra_needles = needles;
      repo_map = build_repo_map_from_indexer(deps_.symbol_indexer, map_opts);
      if (log) {
        log("REPO_MAP +needles: " + std::to_string(repo_map.entries.size()) +
            " símbolos (best_score=" + std::to_string(repo_map.best_score) +
            (map_has_query_hits(repo_map) ? ", query_hits" : ", sin query_hits") + ")");
      }
      if (map_has_query_hits(repo_map)) {
        emit_map_answer(repo_map, "needles");
        return out;
      }
    }

    if (map_has_query_hits(lexical_map)) {
      emit_map_answer(lexical_map, "lexical");
      return out;
    }

    out.ok = true;
    std::ostringstream miss;
    miss << "No encontré métodos claros en el REPO_MAP para esa consulta.\n";
    if (!needles.empty()) {
      miss << "Needles intentados:\n";
      for (const auto& n : needles) {
        miss << "  • " << n << '\n';
      }
    } else {
      miss << "(L1 no propuso needles utilizables)\n";
    }
    out.final_text = miss.str();
    if (force_l2_workflow) {
      // Ask/Plan/Git still hand off: Git has seed context; Ask/Plan can explore via search.
      out.needs_level2 = true;
      if (workflow == AiWorkflowKind::Ask) {
        out.instruction =
            "El mapa rankeado no tuvo hits claros. Usa search/plan para localizar código y "
            "responde con action=synthesize. No edites. Pedido:\n" +
            user_message;
      } else if (workflow == AiWorkflowKind::Plan) {
        out.instruction =
            "El mapa rankeado no tuvo hits claros. Explora con search/plan y emite "
            "action=synthesize con un plan. No edites. Pedido:\n" +
            user_message;
      } else {
        out.instruction =
            "Usa ## Git context (y search si hace falta) para responder. Emite "
            "action=synthesize. No edites. Pedido:\n" +
            user_message;
      }
      out.seeds = needles;
      if (log) {
        log("L1 → l2_handoff via map_miss workflow=" + out.workflow);
        log(out.final_text);
      }
      ai_trace(AiTraceChannel::L1, "l2_handoff",
               std::string("{\"via\":\"map_miss\",\"workflow\":\"") + out.workflow + "\"}");
      return out;
    }
    if (log) {
      log("L1 investigar → sin query_hits tras needles; no se mezcla outline genérico");
      log(out.final_text);
    }
    ai_trace(AiTraceChannel::L1, "l1_map_miss",
             "{\"entries\":" + std::to_string(repo_map.entries.size()) + ",\"note\":\"" +
                 ai_trace_escape(repo_map.note) + "\"}");
    return out;
  }

  RepoMap repo_map = build_repo_map_from_indexer(deps_.symbol_indexer, map_opts);
  if (!repo_map.entries.empty()) {
    user += "\n\n---\n" + repo_map.render_text();
  } else if (log && !repo_map.note.empty()) {
    log("REPO_MAP: " + repo_map.note);
  }

  if (log) {
    log("L1 agent start (max_steps=" + std::to_string(max_steps) + ")");
    if (!repo_map.entries.empty()) {
      log("REPO_MAP: " + std::to_string(repo_map.entries.size()) + " símbolos (best_score=" +
          std::to_string(repo_map.best_score) +
          (repo_map.used_pagerank ? ", PageRank" : "") + ")");
    }
  }

  std::string last_tool_key;
  int same_tool_streak = 0;
  std::string last_observation;
  const bool map_ready = !repo_map.entries.empty();
  const std::string map_fallback =
      map_ready ? repo_map.format_investigate_answer(16) : std::string{};

  auto truncate_obs = [](std::string s, std::size_t max_n = 1500) {
    if (s.size() > max_n) {
      s = s.substr(0, max_n) + "\n…";
    }
    return s;
  };

  auto looks_like_junk_final = [](const std::string& text) {
    if (text.empty()) {
      return true;
    }
    if (text.find("/repomap") != std::string::npos || text.find("/map") != std::string::npos ||
        text.find("git_branches") != std::string::npos ||
        text.find("workspace_symbols") != std::string::npos) {
      return true;
    }
    if (!text.empty() && text[0] == '/') {
      return true;
    }
    const bool cites_path =
        text.find("src/") != std::string::npos || text.find(".cpp") != std::string::npos ||
        text.find(".hpp") != std::string::npos || text.find("Resultados") != std::string::npos;
    return !cites_path && text.size() < 80;
  };

  for (int step = 1; step <= max_steps; ++step) {
    if (cancel != nullptr && cancel->load()) {
      out.error = "cancelado";
      return out;
    }
    if (log) {
      log("L1 step " + std::to_string(step) + "/" + std::to_string(max_steps));
    }

    LlamaCompletionRequest req;
    req.system_prompt = build_system_prompt();
    req.history_text = history;
    if (step == 1) {
      req.user_prompt = user + "\n\nResponde SOLO con un JSON action (sin prosa).";
      if (code_locate && map_ready) {
        req.user_prompt +=
            "\nINVESTIGAR: emite {\"action\":\"final\",\"text\":\"...\"} listando los "
            "métodos/firmas del REPO_MAP más relevantes a la consulta "
            "(ruta:línea + firma). PROHIBIDO search|git_*.";
      } else if (code_locate) {
        req.user_prompt +=
            "\nINVESTIGAR (mapa vacío): puedes search con needles del tema. "
            "PROHIBIDO git_*.";
      }
    } else {
      req.user_prompt =
          "Continúa según la última OBSERVATION. "
          "Emite SOLO un JSON action (tool|final|seeds|needs_level2), sin prosa.";
    }
    req.max_tokens = deps_.settings.level1.max_tokens;
    req.n_ctx = std::max(4096, deps_.settings.level1.n_ctx);
    req.temperature = deps_.settings.level1.temperature;
    req.context_role = "L1";
    req.n_ctx_setting_hint = "ai.level1.n_ctx";

    const auto complete_t0 = std::chrono::steady_clock::now();
    const auto completion = deps_.backend->complete(req, cancel);
    const auto complete_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - complete_t0)
                                 .count();
    ai_trace(AiTraceChannel::L1, "l1_complete",
             "{\"step\":" + std::to_string(step) + ",\"ok\":" + (completion.ok ? "1" : "0") +
                 ",\"duration_ms\":" + std::to_string(complete_ms) + ",\"reply_chars\":" +
                 std::to_string(completion.text.size()) +
                 (completion.ok
                      ? ""
                      : (",\"error\":\"" + ai_trace_escape(completion.error) + "\"")) +
                 "}");
    if (!completion.ok) {
      if (code_locate && !map_fallback.empty()) {
        out.ok = true;
        out.final_text = map_fallback;
        if (log) {
          log("✗ L1 complete: " + completion.error);
          log("L1 final (fallback REPO_MAP): " + out.final_text);
        }
        return out;
      }
      if (!last_observation.empty() && code_locate) {
        out.ok = true;
        out.final_text = last_observation;
        if (out.final_text.size() > 1200) {
          out.final_text = out.final_text.substr(0, 1200) + "\n…";
        }
        if (log) {
          log("✗ L1 complete: " + completion.error);
          log("L1 final (fallback observación): " + out.final_text);
        }
        return out;
      }
      out.error = completion.error;
      ai_trace(AiTraceChannel::L1, "l1_complete_fail",
               "{\"step\":" + std::to_string(step) + ",\"error\":\"" +
                   ai_trace_escape(completion.error) + "\"}");
      if (log) {
        log("✗ L1 complete: " + completion.error);
      }
      return out;
    }
    if (log) {
      log("L1 raw: " + completion.text);
    }
    ai_trace(AiTraceChannel::L1, "l1_raw",
             "{\"step\":" + std::to_string(step) + ",\"raw\":\"" +
                 ai_trace_escape(completion.text) + "\"}");

    const Level1Action action = parse_level1_action(completion.text);
    ai_trace(AiTraceChannel::L1, "l1_action",
             std::string("{\"step\":") + std::to_string(step) + ",\"kind\":\"" +
                 action_kind_name(action.kind) + "\",\"tool\":\"" +
                 ai_trace_escape(action.tool_name) + "\",\"arg\":\"" +
                 ai_trace_escape(action.arg) + "\",\"text\":\"" +
                 ai_trace_escape(action.text) + "\"}");
    history += "<|im_start|>assistant\n" + completion.text.substr(0, 800) + "<|im_end|>\n";

    switch (action.kind) {
      case Level1ActionKind::Tool: {
        if (code_locate && is_git_repo_tool_name(action.tool_name)) {
          ai_trace(AiTraceChannel::L1, "l1_reject_tool",
                   "{\"step\":" + std::to_string(step) + ",\"tool\":\"" +
                       ai_trace_escape(action.tool_name) +
                       "\",\"reason\":\"code_locate_blocks_git\"}");
          if (log) {
            log("✗ rechazado " + action.tool_name + " (investigar → REPO_MAP/final)");
          }
          history +=
              "<|im_start|>user\nRECHAZADO git_*. Emite final con métodos del REPO_MAP "
              "(o search solo si el mapa está vacío).<|im_end|>\n";
          continue;
        }
        if (code_locate && map_ready && action.tool_name == "search") {
          ai_trace(AiTraceChannel::L1, "l1_reject_tool",
                   "{\"step\":" + std::to_string(step) +
                       ",\"tool\":\"search\",\"reason\":\"map_first_no_search\"}");
          if (log) {
            log("✗ rechazado search (investigar con REPO_MAP → final)");
          }
          history +=
              "<|im_start|>user\nRECHAZADO search. Ya tienes REPO_MAP. Emite "
              "{\"action\":\"final\",\"text\":\"...\"} listando métodos/firmas del mapa "
              "relevantes a la consulta.<|im_end|>\n";
          continue;
        }
        if (code_locate && action.tool_name == "repo_map") {
          ai_trace(AiTraceChannel::L1, "l1_reject_tool",
                   "{\"step\":" + std::to_string(step) +
                       ",\"tool\":\"repo_map\",\"reason\":\"map_already_injected\"}");
          if (log) {
            log("✗ rechazado repo_map (ya inyectado; emite final)");
          }
          if (!map_fallback.empty()) {
            out.ok = true;
            out.final_text = map_fallback;
            if (log) {
              log("L1 final (REPO_MAP tras rechazo tool): " + out.final_text);
            }
            return out;
          }
          history +=
              "<|im_start|>user\nRECHAZADO repo_map (ya está en el prompt). Emite "
              "{\"action\":\"final\",\"text\":\"...\"} con métodos del REPO_MAP.<|im_end|>\n";
          continue;
        }
        std::string effective_arg = action.arg;
        if (action.tool_name == "search") {
          const auto expanded = expand_search_needles(action.arg, 12);
          if (!expanded.empty()) {
            effective_arg.clear();
            for (std::size_t i = 0; i < expanded.size(); ++i) {
              if (i) {
                effective_arg.push_back('|');
              }
              effective_arg += expanded[i];
            }
          }
        }
        const std::string tool_key = action.tool_name + "\n" + effective_arg;
        if (tool_key == last_tool_key && !last_observation.empty()) {
          out.ok = true;
          out.final_text = !map_fallback.empty() ? map_fallback : last_observation;
          if (out.final_text.size() > 1200 && map_fallback.empty()) {
            out.final_text = out.final_text.substr(0, 1200) + "\n…";
          }
          ai_trace(AiTraceChannel::L1, "l1_loop_break",
                   "{\"step\":" + std::to_string(step) + ",\"tool\":\"" +
                       ai_trace_escape(action.tool_name) + "\",\"streak\":" +
                       std::to_string(same_tool_streak + 1) + "}");
          if (log) {
            log("L1 auto-final (misma tool repetida): " + action.tool_name);
            log("L1 final: " + out.final_text);
          }
          return out;
        }

        const std::string observation =
            invoke_tool_logged(action.tool_name, action.arg, log);
        ai_trace(AiTraceChannel::L1, "l1_observation",
                 "{\"step\":" + std::to_string(step) + ",\"tool\":\"" +
                     ai_trace_escape(action.tool_name) + "\",\"obs\":\"" +
                     ai_trace_escape(observation, 1200) + "\"}");
        if (log) {
          std::istringstream iss(observation);
          std::string line;
          int n = 0;
          while (std::getline(iss, line)) {
            log(line);
            if (++n >= 80) {
              log("…");
              break;
            }
          }
        }
        if (tool_key == last_tool_key) {
          ++same_tool_streak;
        } else {
          same_tool_streak = 1;
          last_tool_key = tool_key;
        }
        last_observation = observation;
        const bool zero_hits =
            action.tool_name == "search" &&
            (observation.find("quality:empty") != std::string::npos ||
             observation.find("hits: 0") != std::string::npos);
        const bool noisy =
            action.tool_name == "search" && observation.find("quality:noisy") != std::string::npos;
        if (zero_hits) {
          history +=
              "<|im_start|>user\nOBSERVATION:\n" + truncate_obs(observation) +
              "\n\nquality:empty. Emite OTRA search con needles|DISTINTOS "
              "(otras traducciones EN, CamelCase, sin repetir este set). "
              "Si no hay más ideas, action=final diciendo 0 hits.<|im_end|>\n";
        } else if (noisy) {
          history +=
              "<|im_start|>user\nOBSERVATION:\n" + truncate_obs(observation) +
              "\n\nquality:noisy. Emite search más específica "
              "(\"Foo path:src\" o ids más largos). No repitas el mismo set. "
              "Luego final con top_files de src/.<|im_end|>\n";
        } else {
          history +=
              "<|im_start|>user\nOBSERVATION:\n" + truncate_obs(observation) +
              "\n\nResponde ahora con action=final y un text breve. "
              "No vuelvas a llamar " +
              action.tool_name + ".<|im_end|>\n";
        }
        break;
      }
      case Level1ActionKind::Seeds: {
        if (action.seeds.empty()) {
          history +=
              "<|im_start|>user\nOBSERVATION: seeds vacíos; dicta identificadores de "
              "código.<|im_end|>\n";
          break;
        }
        if (log && !action.text.empty()) {
          log("plan: " + action.text);
        }
        const std::string pack = run_seeds_pack(action.seeds, log);
        out.seeds = action.seeds;
        if (log) {
          std::istringstream iss(pack);
          std::string line;
          int n = 0;
          while (std::getline(iss, line)) {
            log(line);
            if (++n >= 120) {
              log("…");
              break;
            }
          }
        }
        history +=
            "<|im_start|>user\nOBSERVATION ContextPack:\n" + truncate_obs(pack, 2000) +
            "<|im_end|>\n";
        break;
      }
      case Level1ActionKind::Final: {
        out.ok = true;
        out.final_text = action.text.empty() ? completion.text : action.text;
        if ((out.final_text.find("Responde SOLO") != std::string::npos ||
             out.final_text.find("archivo_activo:") != std::string::npos ||
             out.final_text.find("exceeds the available context") != std::string::npos ||
             out.final_text.find("INVESTIGAR:") != std::string::npos ||
             (code_locate && looks_like_junk_final(out.final_text))) &&
            !map_fallback.empty()) {
          out.final_text = map_fallback;
        } else if ((out.final_text.find("Responde SOLO") != std::string::npos ||
                    out.final_text.find("archivo_activo:") != std::string::npos ||
                    out.final_text.find("exceeds the available context") != std::string::npos) &&
                   !last_observation.empty()) {
          out.final_text = last_observation;
          if (out.final_text.size() > 1200) {
            out.final_text = out.final_text.substr(0, 1200) + "\n…";
          }
        }
        if (log) {
          log("L1 final: " + out.final_text);
        }
        return out;
      }
      case Level1ActionKind::NeedsLevel2: {
        out.ok = true;
        out.needs_level2 = true;
        out.instruction = action.instruction;
        out.seeds = action.seeds;
        if (log) {
          log("L1 → needs_level2 (dry-run; coder aún no cargado)");
          if (!out.instruction.empty()) {
            log("instruction: " + out.instruction);
          }
        }
        if (!out.seeds.empty()) {
          const std::string pack = run_seeds_pack(out.seeds, log);
          if (log) {
            log("=== L2 dry-run payload ===");
            log("instruction: " + out.instruction);
            std::istringstream iss(pack);
            std::string line;
            int n = 0;
            while (std::getline(iss, line)) {
              log(line);
              if (++n >= 160) {
                log("…");
                break;
              }
            }
          }
        } else if (log) {
          log("(sin seeds; el pack L2 quedaría pobre)");
        }
        out.final_text = "needs_level2 (dry-run only)";
        return out;
      }
      case Level1ActionKind::Error:
      case Level1ActionKind::Unknown:
        if (log) {
          log("✗ " + action.text);
        }
        if (code_locate && !map_fallback.empty()) {
          out.ok = true;
          out.final_text = map_fallback;
          if (log) {
            log("L1 final (fallback REPO_MAP tras JSON inválido): " + out.final_text);
          }
          return out;
        }
        if (code_locate && !last_observation.empty()) {
          out.ok = true;
          out.final_text = last_observation;
          if (out.final_text.size() > 1200) {
            out.final_text = out.final_text.substr(0, 1200) + "\n…";
          }
          if (log) {
            log("L1 final (fallback tras JSON inválido): " + out.final_text);
          }
          return out;
        }
        history +=
            "<|im_start|>user\nOBSERVATION: acción inválida (" + action.text +
            "). Reintenta con UN objeto JSON válido "
            "(action=tool|final|seeds|needs_level2).<|im_end|>\n";
        break;
    }
  }

  if (code_locate && !map_fallback.empty()) {
    out.ok = true;
    out.final_text = map_fallback;
    if (log) {
      log("L1 final (max_steps → REPO_MAP): " + out.final_text);
    }
    return out;
  }

  out.error = "max_steps alcanzado sin final";
  ai_trace(AiTraceChannel::L1, "l1_max_steps",
           "{\"max_steps\":" + std::to_string(max_steps) + "}");
  if (log) {
    log("✗ " + out.error);
  }
  return out;
}

}  // namespace tuide
