#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ai/get_code_of.hpp"
#include "ai/l2_action.hpp"
#include "ai/l2_brain.hpp"
#include "ai/l2_effect_summary.hpp"
#include "ai/l2_effect_slice.hpp"
#include "ai/l2_effect_registry.hpp"
#include "ai/l2_cerca_wake.hpp"
#include "ai/l2_entityness.hpp"
#include "ai/l2_problem_frame.hpp"
#include "ai/l2_explore_a.hpp"
#include "ai/l2_feat.hpp"
#include "ai/l2_think.hpp"
#include "ai/l2_wave.hpp"
#include "ai/l2_wave_bosquejo.hpp"
#include "ai/l2_admin.hpp"
#include "ai/l2_catalog.hpp"
#include "ai/level2_autonomous_loop.hpp"
#include "ai/level2_session.hpp"
#include "ai/search_replace.hpp"
#include "ai/tool_registry.hpp"
#include "ai/ai_types.hpp"
#include "ai/coding_embed_rerank.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/llama_net.hpp"
#include "ai/vector_math.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <queue>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace fs = std::filesystem;
using tuide::AiSettings;
using tuide::AiToolResult;
using tuide::GetCodeOfRequest;

namespace {

std::string json_dump_safe(const nlohmann::json& j, int indent = 2) {
  try {
    return j.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
  } catch (const std::exception&) {
    return nlohmann::json({{"error", "json_dump_failed"}}).dump(indent);
  }
}

}  // namespace
using tuide::Level2AutonomousLoopOpts;
using tuide::Level2BootstrapOpts;
using tuide::Level2Session;
using tuide::ToolRegistry;
using tuide::get_code_of;
using tuide::parse_get_code_of_arg;
using tuide::run_level2_autonomous;

namespace {

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t')) {
    s.pop_back();
  }
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  return s.substr(i);
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string run_cmd(const std::string& cmd) {
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return {};
  }
  std::ostringstream out;
  char buf[4096];
  while (fgets(buf, sizeof(buf), pipe) != nullptr) {
    out << buf;
  }
  pclose(pipe);
  return out.str();
}

std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

bool harness_is_exec(const fs::path& p) {
  return !p.empty() && ::access(p.c_str(), X_OK) == 0;
}

// Same lookup as tuide::resolve_rg without embedding the x86_64 blob: RG_PATH,
// PATH, then ~/.cache/tuide/bundled/rg-*/bin/rg. Bare `rg` is not assumed.
std::string harness_rg_binary() {
  static std::string cached;
  static bool once = false;
  if (once) {
    return cached;
  }
  once = true;
  if (const char* env = std::getenv("RG_PATH"); env != nullptr && env[0] != '\0') {
    if (harness_is_exec(env)) {
      cached = env;
      return cached;
    }
  }
  if (const char* path_env = std::getenv("PATH"); path_env != nullptr && path_env[0] != '\0') {
    std::stringstream stream(path_env);
    std::string dir;
    while (std::getline(stream, dir, ':')) {
      if (dir.empty()) {
        continue;
      }
      const fs::path cand = fs::path(dir) / "rg";
      if (harness_is_exec(cand)) {
        cached = cand.string();
        return cached;
      }
    }
  }
  fs::path cache;
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && xdg[0] != '\0') {
    cache = fs::path(xdg) / "tuide" / "bundled";
  } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    cache = fs::path(home) / ".cache" / "tuide" / "bundled";
  }
  std::error_code ec;
  if (!cache.empty() && fs::is_directory(cache, ec)) {
    for (fs::directory_iterator it(cache, ec); !ec && it != fs::directory_iterator(); ++it) {
      if (!it->is_directory(ec)) {
        continue;
      }
      const std::string name = it->path().filename().string();
      if (name.rfind("rg-", 0) != 0) {
        continue;
      }
      const fs::path cand = it->path() / "bin" / "rg";
      if (harness_is_exec(cand)) {
        cached = cand.string();
        return cached;
      }
    }
  }
  return cached;
}

bool harness_ident_bounded(const std::string& line, const std::string& name) {
  if (name.empty() || line.size() < name.size()) {
    return false;
  }
  auto is_id = [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
  };
  std::size_t pos = 0;
  while (pos < line.size()) {
    const auto hit = line.find(name, pos);
    if (hit == std::string::npos) {
      return false;
    }
    const bool before_ok = hit == 0 || !is_id(static_cast<unsigned char>(line[hit - 1]));
    const auto end = hit + name.size();
    const bool after_ok = end >= line.size() || !is_id(static_cast<unsigned char>(line[end]));
    if (before_ok && after_ok) {
      return true;
    }
    pos = hit + 1;
  }
  return false;
}

std::vector<tuide::ATrailSearchHit> harness_scan_src(const std::string& root,
                                                     const std::string& symbol) {
  std::vector<tuide::ATrailSearchHit> hits;
  if (symbol.empty() || root.empty()) {
    return hits;
  }
  const fs::path src = fs::path(root) / "src";
  std::error_code ec;
  if (!fs::is_directory(src, ec)) {
    return hits;
  }
  for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const std::string ext = it->path().extension().string();
    if (ext != ".cpp" && ext != ".cc" && ext != ".cxx" && ext != ".hpp" && ext != ".h" &&
        ext != ".hh" && ext != ".c") {
      continue;
    }
    std::ifstream in(it->path());
    if (!in) {
      continue;
    }
    std::string rel = it->path().lexically_relative(fs::path(root)).generic_string();
    if (rel.empty() || rel.rfind("..", 0) == 0) {
      rel = "src/" + it->path().filename().string();
    }
    std::string line;
    int n = 0;
    while (std::getline(in, line) && hits.size() < 80) {
      ++n;
      if (!harness_ident_bounded(line, symbol)) {
        continue;
      }
      tuide::ATrailSearchHit h;
      h.path = rel;
      h.line = n;
      h.preview = line;
      hits.push_back(std::move(h));
    }
  }
  return hits;
}

std::string harness_rg_cmd(const std::string& args) {
  const std::string bin = harness_rg_binary();
  if (bin.empty()) {
    return {};
  }
  return shell_quote(bin) + " " + args;
}

void register_read_tools(ToolRegistry* reg, const std::string& root) {
  reg->register_tool("get_code_of", "body", [root](const std::string& arg) {
    GetCodeOfRequest req = parse_get_code_of_arg(arg, root);
    req.workspace_root = root;
    if (req.max_lines <= 0) {
      req.max_lines = 120;
    }
    if (!req.file.empty() && !fs::path(req.file).is_absolute()) {
      req.file = (fs::path(root) / req.file).lexically_normal().string();
    }
    const auto got = get_code_of(req);
    if (!got.ok) {
      return AiToolResult{false, got.error.empty() ? "get_code_of failed" : got.error};
    }
    std::string display = got.path;
    if (!root.empty() && display.size() > root.size() &&
        display.compare(0, root.size(), root) == 0 &&
        (display[root.size()] == '/' || display[root.size()] == '\\')) {
      display = display.substr(root.size() + 1);
    }
    return AiToolResult{true, tuide::format_get_code_of_result(got, display)};
  });

  reg->register_tool("file_outline", "outline", [root](const std::string& arg) {
    const std::string trimmed = trim(arg);
    fs::path abs = trimmed;
    if (!abs.is_absolute()) {
      abs = fs::path(root) / trimmed;
    }
    abs = abs.lexically_normal();
    if (!fs::exists(abs)) {
      return AiToolResult{false, "no existe: " + trimmed};
    }
    const std::string cmd = harness_rg_cmd(
        "-n --no-heading -e "
        "'^[a-zA-Z_].*\\(.*\\)\\s*\\{?\\s*$|^\\s*(class|struct|namespace|enum)\\s+' " +
        shell_quote(abs.string()) + " | head -n 80");
    std::string body = cmd.empty() ? std::string{} : run_cmd(cmd);
    if (body.empty()) {
      body = "(sin outline rg)\n";
    }
    return AiToolResult{true, "outline: " + trimmed + "\n" + body};
  });

  reg->register_tool("search", "rg", [root](const std::string& arg) {
    std::string query = trim(arg);
    std::string path_scope = root;
    // Support "needle path:src/" / "needle path:src/foo.cpp" (probe + trail refresh).
    const auto path_pos = query.rfind(" path:");
    if (path_pos != std::string::npos) {
      std::string path_arg = trim(query.substr(path_pos + 6));
      query = trim(query.substr(0, path_pos));
      if (!path_arg.empty()) {
        fs::path p = path_arg;
        if (!p.is_absolute()) {
          p = fs::path(root) / path_arg;
        }
        p = p.lexically_normal();
        if (fs::exists(p)) {
          path_scope = p.string();
        } else if (path_arg == "src" || path_arg.rfind("src/", 0) == 0 ||
                   path_arg.rfind("src\\", 0) == 0) {
          const fs::path src = (fs::path(root) / "src").lexically_normal();
          if (fs::exists(src)) {
            path_scope = src.string();
          }
        }
      }
    }
    if (query.empty()) {
      return AiToolResult{false, "search: query vacío"};
    }
    const std::string cmd = harness_rg_cmd(
        "-n --no-heading -S -F -g '!build/**' -g '!.git/**' -g '!third_party/**' " +
        shell_quote(query) + " " + shell_quote(path_scope) + " | head -n 80");
    std::string body = cmd.empty() ? std::string{} : run_cmd(cmd);
    if (body.empty()) {
      body = "(sin hits)\n";
    }
    return AiToolResult{true, "q=" + query + "\n" + body};
  });

  reg->register_tool("headers_of", "headers", [root](const std::string& arg) {
    const std::string trimmed = trim(arg);
    fs::path abs = trimmed;
    if (!abs.is_absolute()) {
      abs = fs::path(root) / trimmed;
    }
    abs = abs.lexically_normal();
    if (!fs::exists(abs)) {
      return AiToolResult{false, "no existe: " + trimmed};
    }
    std::ostringstream out;
    out << "headers_of: " << trimmed << '\n';
    std::ifstream in(abs);
    std::string line;
    int n = 0;
    while (in && std::getline(in, line) && n < 24) {
      if (line.find("#include") != std::string::npos) {
        out << line << '\n';
        ++n;
      }
    }
    fs::path sib = abs;
    const std::string ext = sib.extension().string();
    std::string low = ext;
    for (char& c : low) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    if (low == ".cpp" || low == ".cc" || low == ".cxx" || low == ".c") {
      for (const char* hext : {".hpp", ".h", ".hh", ".hxx"}) {
        fs::path cand = sib;
        cand.replace_extension(hext);
        if (fs::exists(cand)) {
          std::string rel = cand.string();
          if (!root.empty() && rel.size() > root.size() &&
              rel.compare(0, root.size(), root) == 0 &&
              (rel[root.size()] == '/' || rel[root.size()] == '\\')) {
            rel = rel.substr(root.size() + 1);
          }
          out << "sibling: " << rel << '\n';
          break;
        }
      }
    }
    if (n == 0) {
      out << "(sin #include)\n";
    }
    return AiToolResult{true, out.str()};
  });

  reg->register_tool("sibling_of", "sibling", [root](const std::string& arg) {
    const std::string trimmed = trim(arg);
    if (trimmed.empty()) {
      return AiToolResult{false, "sibling_of: arg vacío (path relativo)"};
    }
    fs::path abs = trimmed;
    if (!abs.is_absolute()) {
      abs = fs::path(root) / trimmed;
    }
    abs = abs.lexically_normal();
    const std::string ext = abs.extension().string();
    std::string low = ext;
    for (char& c : low) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    std::vector<const char*> cands;
    if (low == ".cpp" || low == ".cc" || low == ".cxx" || low == ".c") {
      cands = {".hpp", ".h", ".hh", ".hxx"};
    } else if (low == ".hpp" || low == ".h" || low == ".hh" || low == ".hxx") {
      cands = {".cpp", ".cc", ".cxx", ".c"};
    } else {
      return AiToolResult{false, "sibling_of: extensión no soportada: " + ext};
    }
    for (const char* hext : cands) {
      fs::path cand = abs;
      cand.replace_extension(hext);
      if (!fs::exists(cand)) {
        continue;
      }
      std::string rel = cand.string();
      if (!root.empty() && rel.size() > root.size() &&
          rel.compare(0, root.size(), root) == 0 &&
          (rel[root.size()] == '/' || rel[root.size()] == '\\')) {
        rel = rel.substr(root.size() + 1);
      }
      return AiToolResult{true, "sibling_of: " + trimmed + " → " + rel + "\n"};
    }
    return AiToolResult{false, "sibling_of: no hay hermano para " + trimmed};
  });

  reg->register_tool("list_tools", "list", [](const std::string&) {
    return AiToolResult{
        true, "get_code_of\nfile_outline\nsearch\nheaders_of\nsibling_of\nlist_tools\n"};
  });
}

bool load_prompt_pack_into_opts(Level2AutonomousLoopOpts* opts, std::string* err) {
  if (opts == nullptr) {
    return false;
  }
  std::string pack_file;
  if (const char* pack_path = std::getenv("L2_PROMPT_PACK");
      pack_path != nullptr && pack_path[0] != '\0') {
    pack_file = pack_path;
  } else {
    // Product default: tools/l2_battery/prompt_packs/DEFAULT_PACK → relative pack name.
    const std::string pointer = read_file(fs::path("tools/l2_battery/prompt_packs/DEFAULT_PACK"));
    std::string name = trim(pointer);
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) {
      name.pop_back();
    }
    if (!name.empty()) {
      fs::path p(name);
      if (!p.is_absolute()) {
        p = fs::path("tools/l2_battery/prompt_packs") / name;
      }
      pack_file = p.string();
    }
  }
  if (pack_file.empty()) {
    return true;
  }
  const std::string raw = read_file(fs::path(pack_file));
  if (raw.empty()) {
    if (err) {
      *err = "prompt pack vacío o ilegible: " + pack_file;
    }
    return false;
  }
  try {
    const auto j = nlohmann::json::parse(raw);
    if (j.contains("system_extra") && j["system_extra"].is_string()) {
      opts->system_prompt_extra = j["system_extra"].get<std::string>();
    }
    if (j.contains("tool_guide") && j["tool_guide"].is_string()) {
      opts->tool_guide_override = j["tool_guide"].get<std::string>();
    }
    if (j.contains("user_overlays") && j["user_overlays"].is_object()) {
      const auto& u = j["user_overlays"];
      if (u.contains("explore") && u["explore"].is_string()) {
        opts->user_overlay_explore = u["explore"].get<std::string>();
      }
      if (u.contains("pack") && u["pack"].is_string()) {
        opts->user_overlay_pack = u["pack"].get<std::string>();
      }
      if (u.contains("edit") && u["edit"].is_string()) {
        opts->user_overlay_edit = u["edit"].get<std::string>();
      }
      if (u.contains("map_review") && u["map_review"].is_string()) {
        opts->user_overlay_map_review = u["map_review"].get<std::string>();
      }
    }
  } catch (const std::exception& e) {
    if (err) {
      *err = std::string("prompt pack JSON: ") + e.what();
    }
    return false;
  }
  return true;
}

void print_ok_line(const tuide::Level2TurnResult& tr, tuide::Level2Session& session,
                   const std::string& root, const std::string& prefix) {
  std::cout << prefix << " turn=" << tr.turn << " phase=" << tr.phase << " — " << tr.summary
            << '\n';
  std::cout << session.status_flags(root) << '\n';
}

void usage() {
  std::cerr << "Usage: l2_harness_cli bootstrap|tool|tools|plan|turn|done|edit|compile|status|run|run-explore|run-explore-a|trail-probe|slice-probe|registry-ingest|registry-stats|hunk-try …\n"
            << "  run-explore            // loop until pack completo → edit (no edit/compile)\n"
            << "  run-explore-a          // Phase A only: peeks → a_judge/a_done (no pack B)\n"
            << "  trail-probe SYM […]    // sin LLM: call-stacks TS de símbolos (search+scopes)\n"
            << "  slice-probe SYM […]    // sin LLM: EffectSlice (mapa+fichas → registro + T*)\n"
            << "  registry-ingest …      // oleada + MERGE en .tuide/effect/registry.sqlite\n"
            << "  registry-refresh --path P\n"
            << "  registry-gc [--dry-run|--apply]\n"
            << "  registry-stats|get|neighbors|path|code|files\n"
            << "  registry-embed [--force]   // nomic: fichas sucias → embeddings\n"
            << "  registry-query TEXT [--match-surface …] [--trails] [--map map.md]\n"
            << "  entityness-probe --query TEXT [--aliases-json F] [--out F]  // entityness table\n"
            << "  wave-bosquejo --terms a,b,c  // sin LLM: foto de barrios para conceptos del piloto\n"
            << "  catalog-build [--force]     // censo espacial .tuide/catalog\n"
            << "  catalog-plano [--query T]   // plano de barrios (+ calor)\n"
            << "  catalog-zoom ID [--query T] // zoom barrio o stem\n"
            << "  catalog-search QUERY        // top fichas slim por calor\n"
            << "  zone-judge-shot --cards FILE --case ID  // 1× LLM sobre causal_judge_v1\n"
            << "  zone-judge-battery --cards-root DIR --out DIR  // un LLM secuencial\n"
            << "  atlas-survey --cards-root DIR --out DIR --only ID  // atlas → LLM inspect → ficha\n"
            << "  wave-explore --out DIR [--case ID|--prompt TEXT] [--think ...] [--control] [--run-jobs] [--script FILE]\n"
            << "  wave-control-shot --out DIR [--from DIR] [--cue default|neutral] [--think …]\n"
            << "  admin-run --out DIR --prompt TEXT [--script FILE]  // admin_v1 (no toca wave-explore)\n"
            << "  admin-turn  (alias de admin-run)\n"
            << "  worker-probe --kind cubre|como|gap --case ID --stem S --out DIR\n"
            << "  trail-judge-shot [SYM] // 1× LLM: trail mapa L0 → a_trail_judge (caso 17)\n"
            << "  dataflow-probe VAR     // sin LLM: writes/reads/decls vía ripgrep (no LSP)\n"
            << "  effect-summary-probe   // sin LLM: fichas TS (SYM|--from-a-state|--from-map)\n"
            << "  a0-sniff-shot          // sin LLM: olfateo A0 con veredictos heurísticos\n"
            << "  a0-sniff-judge-shot    // 1× LLM: effect summary → a_judge (caso 17)\n"
            << "  a0-first-judge-shot    // 1× LLM: solo 1er turno A0; cobertura completa tranche\n"
            << "  a0-tranche-rank-shot   // sin LLM: rerank A0 por cuerpo de ficha ES (slice→card→score)\n"
            << "  card-embed-bench       // mide coste: fichas ES vs cuerpos + coseno embed (top-N mapa)\n"
            << "  plan target [target…]   // watchlist → pack.md\n"
            << "  tools <calls.json>      // batch [{name,arg},…] o {\"calls\":[…]}\n"
            << "  done [summary] [--edit|--clarify]\n"
            << "  edit <hunks.json>\n"
            << "  hunk-try <hunk.json>    // dry-run Search/Replace (no LLM, no write)\n"
            << "  run                     // loop autónomo L2 local (llama-cli)\n";
}

AiSettings load_ai_settings(const std::string& root) {
  AiSettings s;
  s.level2_mode = "local";
  s.models_cache_dir = "/opt/workspace/tuide-models";
  s.level2.model_id = "qwen2.5-coder-1.5b-instruct-q4_k_m";
  s.level2.n_ctx = 4096;
  s.level2.max_steps = 32;
  s.level2.max_tokens = 1024;
  s.level2.temperature = 0.1f;
  const std::string cfg = read_file(fs::path(root) / ".tuide" / "config.json");
  if (cfg.empty()) {
    return s;
  }
  try {
    const auto j = nlohmann::json::parse(cfg);
    if (!j.contains("ai") || !j["ai"].is_object()) {
      return s;
    }
    const auto& ai = j["ai"];
    if (ai.contains("models") && ai["models"].is_object() &&
        ai["models"].contains("cache_dir") && ai["models"]["cache_dir"].is_string()) {
      s.models_cache_dir = ai["models"]["cache_dir"].get<std::string>();
    }
    if (ai.contains("level2") && ai["level2"].is_object()) {
      const auto& l2 = ai["level2"];
      s.level2_mode = l2.value("mode", s.level2_mode);
      s.level2.model_id = l2.value("model_id", s.level2.model_id);
      s.level2.model_path = l2.value("model_path", s.level2.model_path);
      s.level2.cli_path = l2.value("cli_path", s.level2.cli_path);
      s.level2.max_steps = l2.value("max_steps", s.level2.max_steps);
      s.level2.max_tokens = l2.value("max_tokens", s.level2.max_tokens);
      s.level2.n_ctx = l2.value("n_ctx", s.level2.n_ctx);
      s.level2.temperature = l2.value("temperature", s.level2.temperature);
      s.level2.auto_download = l2.value("auto_download", s.level2.auto_download);
      s.level2.clarify_pushback_max =
          l2.value("clarify_pushback_max", s.level2.clarify_pushback_max);
      s.level2.api_base = l2.value("api_base", s.level2.api_base);
      s.level2.api_model = l2.value("api_model", s.level2.api_model);
      s.level2.n_ctx_remote = l2.value("n_ctx_remote", s.level2.n_ctx_remote);
    }
    if (ai.contains("level0") && ai["level0"].is_object() &&
        ai["level0"].contains("embeddings") && ai["level0"]["embeddings"].is_object()) {
      const auto& emb = ai["level0"]["embeddings"];
      s.level0.embeddings.server_host =
          emb.value("server_host", s.level0.embeddings.server_host);
      s.level0.embeddings.server_port =
          emb.value("server_port", s.level0.embeddings.server_port);
    }
  } catch (...) {
  }
  tuide::apply_ai_runtime_env(&s);
  if (s.level2_mode == "remote" && s.level2.n_ctx_remote > 0) {
    s.level2.n_ctx = s.level2.n_ctx_remote;
  }
  return s;
}

std::unique_ptr<tuide::L2Brain> ready_l2_brain(
    const AiSettings& settings, const std::function<void(const std::string&)>& progress,
    std::string* err) {
  if (settings.level2_mode != "local" && settings.level2_mode != "remote") {
    if (err) {
      *err = "ai.level2.mode debe ser local|remote (ahora=" + settings.level2_mode + ")";
    }
    return nullptr;
  }
  auto brain = tuide::make_l2_brain(settings.level2_mode, nullptr);
  if (brain == nullptr) {
    if (err) {
      *err = "no hay L2 brain para mode=" + settings.level2_mode;
    }
    return nullptr;
  }
  if (!brain->ensure_ready(settings, progress, err)) {
    return nullptr;
  }
  return brain;
}

int run_wave_explore(const std::string& root, int argc, char** argv);
int run_wave_control_shot(const std::string& root, int argc, char** argv);

std::vector<tuide::ATrailSearchHit> parse_rg_hits(const std::string& body,
                                                  const std::string& workspace_root) {
  std::vector<tuide::ATrailSearchHit> hits;
  std::istringstream in(body);
  std::string line;
  while (std::getline(in, line)) {
    if (line.size() < 5) {
      continue;
    }
    const auto colon1 = line.find(':');
    if (colon1 == std::string::npos) {
      continue;
    }
    const auto colon2 = line.find(':', colon1 + 1);
    if (colon2 == std::string::npos) {
      continue;
    }
    const std::string line_s = line.substr(colon1 + 1, colon2 - colon1 - 1);
    bool digits = !line_s.empty();
    for (char c : line_s) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        digits = false;
        break;
      }
    }
    if (!digits) {
      continue;
    }
    tuide::ATrailSearchHit h;
    h.path = line.substr(0, colon1);
    h.line = std::atoi(line_s.c_str());
    h.preview = line.substr(colon2 + 1);
    if (!workspace_root.empty()) {
      std::error_code ec;
      const fs::path wp = fs::path(workspace_root).lexically_normal();
      fs::path p = fs::path(h.path);
      if (p.is_relative()) {
        p = wp / p;
      }
      p = p.lexically_normal();
      const auto rel = p.lexically_relative(wp);
      const std::string rs = rel.generic_string();
      if (!rs.empty() && rs != "." && rs.rfind("..", 0) != 0) {
        h.path = rs;
      }
    }
    if (h.path.rfind("./", 0) == 0) {
      h.path = h.path.substr(2);
    }
    hits.push_back(std::move(h));
  }
  return hits;
}

std::vector<tuide::ATrailSearchHit> filter_src_trail_hits(
    const std::vector<tuide::ATrailSearchHit>& in) {
  std::vector<tuide::ATrailSearchHit> out;
  out.reserve(in.size());
  for (const auto& h : in) {
    if (h.path.rfind("tests/", 0) == 0 || h.path.rfind("tools/", 0) == 0 ||
        h.path.rfind("docs/", 0) == 0) {
      continue;
    }
    if (h.path.rfind("src/", 0) == 0) {
      out.push_back(h);
    }
  }
  return out;
}

void harness_append_symbol_hits(std::vector<tuide::ATrailSearchHit>* hits,
                                std::unordered_set<std::string>* seen, const std::string& root,
                                const std::string& symbol);

std::vector<tuide::ATrailSearchHit> harness_search_symbol(ToolRegistry* tools,
                                                          const std::string& root,
                                                          const std::string& symbol) {
  std::unordered_set<std::string> seen;
  std::vector<tuide::ATrailSearchHit> hits;
  if (tools != nullptr && tools->has("search") && !symbol.empty()) {
    AiToolResult tr = tools->invoke("search", symbol + " path:src/");
    if (!tr.ok || tr.text.find("(sin hits)") != std::string::npos) {
      tr = tools->invoke("search", symbol);
    }
    if (tr.ok) {
      for (auto& h : filter_src_trail_hits(parse_rg_hits(tr.text, root))) {
        const std::string key = h.path + ":" + std::to_string(h.line);
        if (seen.insert(key).second) {
          hits.push_back(std::move(h));
        }
      }
    }
  }
  harness_append_symbol_hits(&hits, &seen, root, symbol);
  return hits;
}

void harness_append_symbol_hits(std::vector<tuide::ATrailSearchHit>* hits,
                                std::unordered_set<std::string>* seen, const std::string& root,
                                const std::string& symbol) {
  if (hits == nullptr || seen == nullptr || symbol.empty()) {
    return;
  }
  const fs::path src_dir = fs::path(root) / "src";
  const std::string cmd = harness_rg_cmd(
      "-n --no-heading -F " + shell_quote(symbol) + " " +
      shell_quote(src_dir.lexically_normal().string()) + " 2>/dev/null | head -80");
  std::vector<tuide::ATrailSearchHit> raw;
  if (!cmd.empty()) {
    raw = filter_src_trail_hits(parse_rg_hits(run_cmd(cmd), root));
  }
  if (raw.empty()) {
    raw = harness_scan_src(root, symbol);
  }
  for (auto& h : raw) {
    const std::string key = h.path + ":" + std::to_string(h.line);
    if (seen->insert(key).second) {
      hits->push_back(std::move(h));
    }
  }
}

std::vector<tuide::ATrailSearchHit> harness_rg_symbol(const std::string& root,
                                                      const std::string& symbol) {
  std::vector<tuide::ATrailSearchHit> hits;
  std::unordered_set<std::string> seen;
  harness_append_symbol_hits(&hits, &seen, root, symbol);
  return hits;
}

void harness_push_causal_hop(std::vector<tuide::WaveHit>* hops, const std::string& path,
                             const std::string& symbol) {
  if (hops == nullptr || symbol.empty()) {
    return;
  }
  tuide::WaveHit w;
  w.path = path;
  w.symbol = symbol;
  w.stem = tuide::registry_stem_of(path);
  w.kind = "fn";
  w.needle = "follow";
  w.id = path.empty() ? symbol : (path + ":" + symbol);
  hops->push_back(std::move(w));
}

// Same payload as worker tool `causal`: stacks + cond branches + mermaid (code edges).
std::string harness_causal_flow_markdown(
    const std::string& root, const std::string& focus_path, const std::string& focus_sym,
    const std::function<std::vector<tuide::ATrailSearchHit>(const std::string&)>& search,
    std::vector<tuide::WaveHit>* hops) {
  const auto stacks = tuide::a_trail_build_full_stacks(root, focus_sym, focus_path, search);
  const auto branches = tuide::a_trail_build_cond_branches(
      root, focus_sym, focus_path, std::vector<std::string>{focus_sym}, search, stacks);
  std::vector<std::pair<std::string, std::string>> edges;
  for (const auto& st : stacks) {
    for (std::size_t i = 0; i + 1 < st.hops.size(); ++i) {
      const std::string a =
          st.hops[i].symbol.empty() ? st.hops[i].anchor : st.hops[i].symbol;
      const std::string b =
          st.hops[i + 1].symbol.empty() ? st.hops[i + 1].anchor : st.hops[i + 1].symbol;
      if (!a.empty() && !b.empty()) {
        edges.push_back({a, b});
      }
    }
    for (const auto& hop : st.hops) {
      const std::string name = hop.symbol.empty() ? hop.anchor : hop.symbol;
      harness_push_causal_hop(hops, hop.path, name);
    }
  }
  for (const auto& b : branches) {
    if (!b.symbol.empty() && !focus_sym.empty()) {
      edges.push_back({focus_sym, b.symbol});
    }
    if (!b.path.empty() && !b.symbol.empty()) {
      harness_push_causal_hop(hops, b.path, b.symbol);
    }
  }
  std::ostringstream chunk;
  chunk << tuide::a_trail_causal_flow_markdown(stacks, branches);
  chunk << tuide::registry_causal_pilot_causal_mermaid(edges);
  const std::string arg = focus_path.empty() ? focus_sym : (focus_path + ":" + focus_sym);
  tuide::GetCodeOfRequest creq = tuide::parse_get_code_of_arg(arg, root);
  creq.workspace_root = root;
  if (creq.max_lines <= 0) {
    creq.max_lines = 200;
  }
  const tuide::GetCodeOfResult got = tuide::get_code_of(creq);
  if (got.ok && !got.text.empty()) {
    std::string span = got.text;
    const auto nl = span.find('\n');
    if (nl != std::string::npos) {
      const std::string head = span.substr(0, nl);
      if (head.find('/') != std::string::npos || head.find(".cpp") != std::string::npos ||
          head.find(".hpp") != std::string::npos) {
        span = span.substr(nl + 1);
      }
    }
    const auto outs = tuide::wave_follow_outgoing_calls(span);
    chunk << tuide::wave_follow_outgoing_markdown(arg, outs);
    const std::string hop_path = focus_path.empty() ? got.path : focus_path;
    for (const auto& c : outs) {
      harness_push_causal_hop(hops, hop_path, c.symbol);
    }
  }
  return chunk.str();
}

std::vector<std::string> harness_split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : text) {
    if (c == '\n') {
      lines.push_back(std::move(cur));
      cur.clear();
    } else if (c != '\r') {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) {
    lines.push_back(std::move(cur));
  }
  return lines;
}

std::string harness_compact_src(std::string s, std::size_t n = 96) {
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  s = s.substr(i);
  if (s.size() > n) {
    s.resize(n);
    s += "…";
  }
  return s;
}

bool harness_in_body_search(const std::string& root, const std::string& path,
                            const std::string& symbol, const std::vector<std::string>& needles,
                            std::string* md, std::vector<int>* hits_per_needle,
                            std::string* err) {
  if (md == nullptr) {
    if (err) {
      *err = "md nulo";
    }
    return false;
  }
  const std::string arg = path.empty() ? symbol : (path + ":" + symbol);
  tuide::GetCodeOfRequest req = tuide::parse_get_code_of_arg(arg, root);
  req.workspace_root = root;
  req.max_lines = 12;
  req.window = tuide::GetCodeOfWindow::Head;
  auto got = tuide::get_code_of(req);
  if (!got.ok || got.symbol_start <= 0) {
    if (err) {
      *err = got.error.empty() ? "in: no hay span de símbolo" : got.error;
    }
    return false;
  }
  const int span = std::max(1, got.symbol_end - got.symbol_start + 1);
  req.window = tuide::GetCodeOfWindow::Range;
  req.range_start = got.symbol_start;
  req.range_end = got.symbol_end;
  req.max_lines = span;
  got = tuide::get_code_of(req);
  if (!got.ok || got.text.empty()) {
    if (err) {
      *err = got.error.empty() ? "in: cuerpo vacío" : got.error;
    }
    return false;
  }
  const auto lines = harness_split_lines(got.text);
  const int line0 = got.sent_start > 0 ? got.sent_start : got.symbol_start;
  const std::string rel = got.path.empty() ? path : got.path;
  fs::path abs = fs::path(root) / rel;
  const std::string abs_s = abs.lexically_normal().string();
  constexpr int kHead = 8;
  constexpr int kTail = 6;
  constexpr int kMaxHits = 8;
  std::ostringstream out;
  const std::string display = rel.empty() ? symbol : (rel + ":" + symbol);
  out << "----- in " << display << " -----\n";
  if (!got.name.empty()) {
    out << "sig: " << got.name;
    if (got.symbol_start > 0) {
      out << "  L" << got.symbol_start << "-" << got.symbol_end;
    }
    out << "\n";
  }
  auto emit_range = [&](int from_ln, int to_ln) {
    out << "```\n";
    for (int ln = from_ln; ln <= to_ln; ++ln) {
      const int idx = ln - line0;
      if (idx < 0 || idx >= static_cast<int>(lines.size())) {
        continue;
      }
      out << ln << "| " << lines[static_cast<std::size_t>(idx)] << "\n";
    }
    out << "```\n";
  };
  const int last_ln = line0 + static_cast<int>(lines.size()) - 1;
  const bool short_fn = static_cast<int>(lines.size()) <= kHead + kTail + 2;
  if (short_fn) {
    emit_range(line0, last_ln);
  } else {
    emit_range(line0, line0 + kHead - 1);
  }
  int total_hits = 0;
  for (const auto& needle : needles) {
    int n = 0;
    std::ostringstream hits_out;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
      if (!tuide::wave_line_has_needle(lines[static_cast<std::size_t>(i)], needle)) {
        continue;
      }
      ++n;
      if (n > kMaxHits) {
        continue;
      }
      const int ln = line0 + i;
      tuide::ATrailHop hop = tuide::a_trail_enrich_hop(abs_s, rel, ln, "");
      std::string scopes = hop.control_chain;
      if (scopes.empty()) {
        scopes = hop.control_kind;
      }
      if (scopes.empty()) {
        scopes = hop.scope_chain;
      }
      hits_out << "  L" << ln;
      if (!scopes.empty()) {
        hits_out << " [" << scopes << "]";
      }
      hits_out << "  `" << harness_compact_src(lines[static_cast<std::size_t>(i)]) << "`\n";
    }
    if (hits_per_needle != nullptr) {
      hits_per_needle->push_back(n);
    }
    total_hits += n;
    out << needle << " hits=" << n;
    if (n > kMaxHits) {
      out << " (mostrando " << kMaxHits << ")";
    }
    out << "\n";
    out << hits_out.str();
    if (n == 0) {
      out << "  (sin match en este cuerpo)\n";
    }
  }
  if (!short_fn) {
    out << "… cuerpo L" << (line0 + kHead) << "-" << (last_ln - kTail) << " …\n";
    emit_range(last_ln - kTail + 1, last_ln);
  }
  (void)total_hits;
  *md = out.str();
  return true;
}

int run_trail_probe(ToolRegistry* tools, const std::string& root, int argc, char** argv) {
  std::vector<std::string> symbols;
  std::string path_hint;
  bool json_out = false;
  bool persist = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--path" && i + 1 < argc) {
      path_hint = argv[++i];
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "--persist") {
      persist = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "trail-probe SYM [SYM…] [--path hint] [--json] [--persist]\n"
                   "  Sin LLM. search+TS → pilas entry→…→focus con scopes/control/snippet.\n"
                   "  --persist escribe a_state.json con trail (round-trip storage).\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      symbols.push_back(a);
    }
  }
  if (symbols.empty()) {
    std::cerr << "trail-probe: falta SYM\n";
    return 2;
  }

  int rc = 0;
  for (const auto& sym : symbols) {
    auto search_fn = [&](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
      return harness_search_symbol(tools, root, symbol);
    };

    const auto direct = search_fn(sym);
    const auto stacks = tuide::a_trail_build_full_stacks(root, sym, path_hint, search_fn,
                                                         tuide::kATrailMaxStacks,
                                                         tuide::kATrailMaxDepth);

    tuide::ATrail tr;
    tr.active = true;
    tr.awaiting_judge = true;
    tr.root_anchor = path_hint.empty() ? sym : (path_hint + ":" + sym);
    tr.focus_anchor = tr.root_anchor;
    tr.focus_symbol = sym;
    tr.root_stem = sym;
    tr.pending_stacks = stacks;
    tr.cond_branches = tuide::a_trail_build_cond_branches(
        root, sym, path_hint, {sym}, search_fn, stacks);
    tuide::ATrailHop root_hop;
    root_hop.symbol = sym;
    root_hop.anchor = tr.root_anchor;
    root_hop.path = path_hint;
    root_hop.summary = "L0 probe";
    tr.trail.push_back(root_hop);

    if (json_out) {
      nlohmann::json j;
      j["symbol"] = sym;
      j["path_hint"] = path_hint;
      j["direct_hits"] = static_cast<int>(direct.size());
      j["stacks"] = static_cast<int>(stacks.size());
      j["trail"] = tuide::a_trail_to_json(tr);
      std::cout << j.dump(2) << '\n';
    } else {
      std::cout << "======== trail-probe `" << sym << "` ========\n";
      std::cout << "direct_hits=" << direct.size() << " stacks=" << stacks.size()
                << " max_depth=" << tuide::kATrailMaxDepth << "\n";
      if (!direct.empty()) {
        std::cout << "top direct callers:\n";
        for (std::size_t i = 0; i < std::min<std::size_t>(8, direct.size()); ++i) {
          const auto& h = direct[i];
          std::cout << "  - " << h.path << ":" << h.line << "  "
                    << h.preview.substr(0, std::min<std::size_t>(90, h.preview.size())) << "\n";
        }
      }
      std::cout << "\n" << tuide::a_trail_stacks_markdown(tr);
      // Compact chain lines
      if (!stacks.empty()) {
        std::cout << "### Chains (compact)\n";
        for (const auto& s : stacks) {
          std::cout << s.id << ": ";
          for (std::size_t i = 0; i < s.hops.size(); ++i) {
            if (i) {
              std::cout << " → ";
            }
            const auto& h = s.hops[i];
            std::cout << h.symbol;
            if (!h.control_chain.empty()) {
              std::cout << "@{" << h.control_chain << "}";
            } else if (!h.control_kind.empty()) {
              std::cout << "@" << h.control_kind;
            }
            if (!h.scope_chain.empty() && i + 1 < s.hops.size()) {
              std::cout << " [" << h.scope_chain << "]";
            }
          }
          std::cout << "\n";
        }
        std::cout << "\n";
      }
    }

    if (stacks.empty() && direct.empty()) {
      std::cerr << "trail-probe: sin hits para `" << sym << "`\n";
      rc = 1;
    }

    if (persist) {
      tuide::AState ast;
      ast.trail = tr;
      tuide::AVerdict u;
      u.target = tr.root_anchor;
      u.anchor = tr.root_anchor;
      u.stem = sym;
      u.verdict = tuide::AVerdictKind::Useful;
      u.why = "trail-probe";
      ast.notes.push_back(u);
      std::string err;
      if (!Level2Session::save_a_state(root, ast, &err)) {
        std::cerr << "persist fail: " << err << '\n';
        rc = 1;
      } else {
        const auto loaded = Level2Session::load_a_state(root);
        std::cout << "persist ok: a_state trail.active=" << (loaded.trail.active ? 1 : 0)
                  << " stacks=" << loaded.trail.pending_stacks.size()
                  << " path=" << Level2Session::a_state_path(root) << "\n";
      }
    }
  }
  return rc;
}

std::vector<std::string> parse_seeds_csv(const std::string& csv);

bool parse_slice_cli_args(int argc, char** argv, std::vector<std::string>* symbols, std::string* path_hint,
                          std::string* map_path, std::string* seeds_csv, bool* json_out, bool* expand,
                          bool* siblings, int* k, int* top_n, bool* commit, const char* help) {
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--path" && i + 1 < argc) {
      *path_hint = argv[++i];
    } else if (a == "--from-map" && i + 1 < argc) {
      *map_path = argv[++i];
    } else if (a == "--seeds" && i + 1 < argc) {
      *seeds_csv = argv[++i];
    } else if (a == "--top" && i + 1 < argc) {
      *top_n = std::atoi(argv[++i]);
    } else if (a == "--json") {
      *json_out = true;
    } else if (a == "--expand") {
      *expand = true;
    } else if (a == "--no-siblings") {
      *siblings = false;
    } else if (a == "--commit") {
      if (commit) {
        *commit = true;
      }
    } else if (a == "--k" && i + 1 < argc) {
      *k = std::atoi(argv[++i]);
    } else if (a == "-h" || a == "--help") {
      std::cerr << help;
      return false;
    } else if (!a.empty() && a[0] != '-') {
      symbols->push_back(a);
    }
  }
  return true;
}

bool build_slice_from_cli(const std::string& root, const std::vector<std::string>& symbols,
                          const std::string& path_hint, const std::string& map_path,
                          const std::string& seeds_csv, int top_n, bool siblings, bool expand,
                          tuide::EffectSlice* sl, tuide::RegistryIngestMeta* meta, std::string* err) {
  tuide::EffectSliceSeedIn in;
  in.query = meta && !meta->query.empty() ? meta->query : "slice-probe";
  in.add_siblings = siblings;
  in.seeds = parse_seeds_csv(seeds_csv);
  in.window_n = tuide::kEffectSliceMaxSeedFn;
  if (!map_path.empty()) {
    fs::path mp(map_path);
    if (!mp.is_absolute()) {
      mp = fs::path(root) / map_path;
    }
    const std::string md = read_file(mp);
    if (md.empty()) {
      if (err) {
        *err = "no se pudo leer " + mp.string();
      }
      return false;
    }
    tuide::effect_slice_fill_seed_from_map(&in, md, top_n);
  }
  for (const auto& sym : symbols) {
    tuide::EffectSliceSeedFn fn;
    fn.symbol = sym;
    fn.path = path_hint;
    fn.prior_sem = 0.9f;
    in.map_window.push_back(std::move(fn));
  }
  if (in.map_window.empty() && in.inventory_paths.empty()) {
    if (err) {
      *err = "falta SYM o --from-map";
    }
    return false;
  }
  if (!tuide::effect_slice_seed(sl, in, err)) {
    return false;
  }
  tuide::EffectSliceDeps deps;
  deps.workspace_root = root;
  deps.search = [root](const std::string& symbol) { return harness_rg_symbol(root, symbol); };
  if (!tuide::effect_slice_build(sl, deps, err)) {
    return false;
  }
  if (expand) {
    (void)tuide::effect_slice_expand(sl, deps, err);
  }
  if (meta) {
    meta->query = in.query;
    meta->seeds = in.seeds;
    meta->map_path = map_path;
  }
  return true;
}

int commit_slice_to_registry(const std::string& root, const tuide::EffectSlice& sl,
                             const tuide::RegistryIngestMeta& meta) {
  tuide::EffectRegistry reg;
  std::string err;
  if (!tuide::registry_open(root, &reg, &err)) {
    std::cerr << "registry-open: " << err << "\n";
    return 1;
  }
  const bool ok = tuide::registry_ingest_slice(&reg, sl, meta, &err);
  tuide::registry_close(&reg);
  if (!ok) {
    std::cerr << "registry-ingest: " << err << "\n";
    return 1;
  }
  std::cout << "registry commit → " << tuide::registry_db_path(root) << "\n";
  return 0;
}

int run_slice_probe(ToolRegistry* tools, const std::string& root, int argc, char** argv) {
  (void)tools;
  std::vector<std::string> symbols;
  std::string path_hint;
  std::string map_path;
  std::string seeds_csv;
  bool json_out = false;
  bool expand = false;
  bool siblings = true;
  bool commit = false;
  int k = tuide::kEffectSliceMaxThreads;
  int top_n = tuide::kEffectSliceMapTopDefault;
  if (!parse_slice_cli_args(argc, argv, &symbols, &path_hint, &map_path, &seeds_csv, &json_out,
                            &expand, &siblings, &k, &top_n, &commit,
                            "slice-probe SYM [SYM…] [--path hint] [--from-map map.md] [--top N]\n"
                            "  [--seeds a,b,c] [--json] [--expand] [--no-siblings] [--k N] [--commit]\n"
                            "  Sin LLM. Mapa rankeado + fichas A0 → registro conjunto + hilos T*.\n"
                            "  --commit escribe .tuide/effect/registry.sqlite\n")) {
    return 2;
  }
  tuide::EffectSlice sl;
  tuide::RegistryIngestMeta meta;
  meta.query = "slice-probe";
  std::string err;
  const auto t0 = std::chrono::steady_clock::now();
  if (!build_slice_from_cli(root, symbols, path_hint, map_path, seeds_csv, top_n, siblings, expand,
                            &sl, &meta, &err)) {
    std::cerr << "slice-probe: " << err << "\n";
    return err == "falta SYM o --from-map" ? 2 : 1;
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  (void)k;
  if (json_out) {
    auto j = tuide::effect_slice_to_json(sl);
    j["build_ms"] = ms;
    std::cout << j.dump(2) << "\n";
  } else {
    std::cout << "======== slice-probe ========\n";
    std::cout << "build_ms=" << ms << "\n";
    std::cout << tuide::effect_slice_view_markdown(sl);
    std::cout << "\n### Nodos (resumen)\n";
    int shown = 0;
    for (const auto& n : sl.nodes) {
      if (shown >= 80) {
        std::cout << "…\n";
        break;
      }
      std::cout << "  " << tuide::effect_node_kind_name(n.kind);
      if (n.kind == tuide::EffectNodeKind::Ctrl) {
        std::cout << "/" << tuide::effect_ctrl_kind_name(n.ctrl_kind);
      }
      std::cout << "  " << (n.symbol.empty() ? n.id : n.symbol);
      if (!n.path.empty() && n.kind == tuide::EffectNodeKind::Fn) {
        std::cout << "  [" << n.path << ":" << n.line << "]";
      }
      if (n.mass > 0.001f) {
        std::cout << "  mass=" << n.mass;
      }
      std::cout << "\n";
      ++shown;
    }
  }
  if (commit) {
    const int rc = commit_slice_to_registry(root, sl, meta);
    if (rc != 0) {
      return rc;
    }
  }
  return sl.nodes.empty() ? 1 : 0;
}

bool open_registry_or_die(const std::string& root, tuide::EffectRegistry* r) {
  std::string err;
  if (!tuide::registry_open(root, r, &err)) {
    std::cerr << "registry-open: " << err << "\n";
    return false;
  }
  return true;
}

int run_registry_ingest(const std::string& root, int argc, char** argv) {
  std::vector<std::string> symbols;
  std::string path_hint;
  std::string map_path;
  std::string seeds_csv;
  bool json_out = false;
  bool expand = false;
  bool siblings = true;
  int k = tuide::kEffectSliceMaxThreads;
  int top_n = tuide::kEffectSliceMapTopDefault;
  if (!parse_slice_cli_args(argc, argv, &symbols, &path_hint, &map_path, &seeds_csv, &json_out,
                            &expand, &siblings, &k, &top_n, nullptr,
                            "registry-ingest --from-map map.md [--top N] [--seeds a,b] [SYM…]\n"
                            "  [--path hint] [--no-siblings] [--json]\n"
                            "  Oleada (effect_slice_build) + MERGE en .tuide/effect/registry.sqlite\n")) {
    return 2;
  }
  tuide::EffectSlice sl;
  tuide::RegistryIngestMeta meta;
  meta.query = "registry-ingest";
  std::string err;
  if (!build_slice_from_cli(root, symbols, path_hint, map_path, seeds_csv, top_n, siblings, expand,
                            &sl, &meta, &err)) {
    std::cerr << "registry-ingest: " << err << "\n";
    return err == "falta SYM o --from-map" ? 2 : 1;
  }
  const int rc = commit_slice_to_registry(root, sl, meta);
  if (rc != 0) {
    return rc;
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  tuide::RegistryStats st;
  tuide::registry_stats(&reg, &st, &err);
  if (json_out) {
    std::cout << tuide::registry_stats_to_json(st).dump(2) << "\n";
  } else {
    std::cout << tuide::registry_stats_to_json(st).dump(2) << "\n";
  }
  tuide::registry_close(&reg);
  return 0;
}

int run_registry_refresh(const std::string& root, int argc, char** argv) {
  std::string path;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if ((a == "--path" && i + 1 < argc) || (a == "--path")) {
      if (a == "--path" && i + 1 < argc) {
        path = argv[++i];
      }
    } else if (a == "-h" || a == "--help") {
      std::cerr << "registry-refresh --path src/foo.cpp\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      path = a;
    }
  }
  if (path.empty()) {
    std::cerr << "registry-refresh: falta --path\n";
    return 2;
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  std::string err;
  const bool ok = tuide::registry_refresh_path(&reg, path, &err);
  tuide::registry_close(&reg);
  if (!ok) {
    std::cerr << "registry-refresh: " << err << "\n";
    return 1;
  }
  std::cout << "refreshed " << path << "\n";
  return 0;
}

int run_registry_gc(const std::string& root, int argc, char** argv) {
  tuide::RegistryGcOpts opts;
  opts.dry_run = true;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--apply") {
      opts.dry_run = false;
    } else if (a == "--dry-run") {
      opts.dry_run = true;
    } else if (a == "--min-age" && i + 1 < argc) {
      opts.min_age_queries = std::atoi(argv[++i]);
    } else if (a == "-h" || a == "--help") {
      std::cerr << "registry-gc [--dry-run] [--apply] [--min-age N]\n";
      return 2;
    }
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  tuide::RegistryGcReport report;
  std::string err;
  const bool ok = tuide::registry_gc(&reg, opts, &report, &err);
  tuide::registry_close(&reg);
  if (!ok) {
    std::cerr << "registry-gc: " << err << "\n";
    return 1;
  }
  nlohmann::json j = {{"dry_run", opts.dry_run},
                      {"tombstones", report.tombstones},
                      {"facts_dropped", report.facts_dropped},
                      {"applied", report.applied}};
  std::cout << j.dump(2) << "\n";
  return 0;
}

int run_registry_stats(const std::string& root, int argc, char** argv) {
  (void)argc;
  (void)argv;
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  tuide::RegistryStats st;
  std::string err;
  const bool ok = tuide::registry_stats(&reg, &st, &err);
  tuide::registry_close(&reg);
  if (!ok) {
    std::cerr << "registry-stats: " << err << "\n";
    return 1;
  }
  std::cout << tuide::registry_stats_to_json(st).dump(2) << "\n";
  return 0;
}

int run_registry_get(const std::string& root, int argc, char** argv) {
  std::string id;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      std::cerr << "registry-get ID\n";
      return 2;
    }
    if (!a.empty() && a[0] != '-') {
      id = a;
    }
  }
  if (id.empty()) {
    std::cerr << "registry-get: falta ID\n";
    return 2;
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  tuide::RegistryNodeRow row;
  std::string err;
  const bool ok = tuide::registry_get(&reg, id, &row, &err);
  tuide::registry_close(&reg);
  if (!ok) {
    std::cerr << "registry-get: " << err << "\n";
    return 1;
  }
  std::cout << tuide::registry_node_to_json(row).dump(2) << "\n";
  return 0;
}

int run_registry_neighbors(const std::string& root, int argc, char** argv) {
  std::string id;
  std::string dir;
  std::string kinds_csv;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--kind" && i + 1 < argc) {
      kinds_csv = argv[++i];
    } else if (a == "--dir" && i + 1 < argc) {
      dir = argv[++i];
    } else if (a == "--json") {
      continue;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "registry-neighbors ID [--kind call,write,read,handoff] [--dir out|in]\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      id = a;
    }
  }
  if (id.empty()) {
    std::cerr << "registry-neighbors: falta ID\n";
    return 2;
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  std::vector<tuide::RegistryNeighbor> nbs;
  std::string err;
  const bool ok =
      tuide::registry_neighbors(&reg, id, parse_seeds_csv(kinds_csv), dir, &nbs, &err);
  tuide::registry_close(&reg);
  if (!ok) {
    std::cerr << "registry-neighbors: " << err << "\n";
    return 1;
  }
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& nb : nbs) {
    arr.push_back({{"from", nb.fact.from_id},
                   {"to", nb.fact.to_id},
                   {"kind", nb.fact.kind},
                   {"member", nb.fact.member},
                   {"dir", nb.outbound ? "out" : "in"},
                   {"node", tuide::registry_node_to_json(nb.node)}});
  }
  std::cout << arr.dump(2) << "\n";
  return 0;
}

int run_registry_path(const std::string& root, int argc, char** argv) {
  std::vector<std::string> ids;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      std::cerr << "registry-path FROM TO\n";
      return 2;
    }
    if (!a.empty() && a[0] != '-') {
      ids.push_back(a);
    }
  }
  if (ids.size() < 2) {
    std::cerr << "registry-path: falta FROM TO\n";
    return 2;
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  std::vector<std::string> path;
  std::string err;
  const bool ok = tuide::registry_path_between(&reg, ids[0], ids[1], &path, &err);
  tuide::registry_close(&reg);
  if (!ok) {
    std::cerr << "registry-path: " << err << "\n";
    return 1;
  }
  nlohmann::json j = {{"path", path}};
  std::cout << j.dump(2) << "\n";
  return 0;
}

int run_registry_code(const std::string& root, int argc, char** argv) {
  std::string id;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      std::cerr << "registry-code ID\n";
      return 2;
    }
    if (!a.empty() && a[0] != '-') {
      id = a;
    }
  }
  if (id.empty()) {
    std::cerr << "registry-code: falta ID\n";
    return 2;
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  tuide::RegistryNodeRow row;
  std::string err;
  const bool ok = tuide::registry_get(&reg, id, &row, &err);
  tuide::registry_close(&reg);
  if (!ok) {
    std::cerr << "registry-code: " << err << "\n";
    return 1;
  }
  tuide::GetCodeOfRequest req;
  req.workspace_root = root;
  req.file = row.path;
  req.symbol = row.symbol;
  req.line = row.line;
  const auto got = tuide::get_code_of(req);
  if (!got.ok) {
    std::cerr << "registry-code: " << got.error << "\n";
    return 1;
  }
  std::cout << tuide::format_get_code_of_result(got, row.path);
  return 0;
}

int run_registry_files(const std::string& root, int argc, char** argv) {
  bool pending_only = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--pending" || a == "pending-inventory") {
      pending_only = true;
    }
  }
  (void)argv;
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  std::string err;
  nlohmann::json arr = nlohmann::json::array();
  if (pending_only) {
    std::vector<std::string> paths;
    tuide::registry_pending_files(&reg, &paths, &err);
    for (const auto& p : paths) {
      arr.push_back({{"path", p}, {"pending_inventory", true}});
    }
  } else {
    std::vector<std::pair<std::string, bool>> files;
    tuide::registry_list_files(&reg, &files, &err);
    for (const auto& f : files) {
      arr.push_back({{"path", f.first}, {"pending_inventory", f.second}});
    }
  }
  tuide::registry_close(&reg);
  std::cout << arr.dump(2) << "\n";
  return 0;
}

bool ensure_embed_backend(const std::string& root, tuide::EmbeddingBackend* backend,
                           std::string* err) {
  const AiSettings settings = load_ai_settings(root);
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  return backend->ensure_ready(settings, progress, err);
}

int run_registry_embed(const std::string& root, int argc, char** argv) {
  tuide::RegistryEmbedOpts opts;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--force") {
      opts.force = true;
    } else if (a == "--all") {
      opts.skip_glue = false;
    } else if (a == "--model" && i + 1 < argc) {
      opts.model = argv[++i];
    } else if (a == "--max" && i + 1 < argc) {
      opts.max_nodes = std::atoi(argv[++i]);
    } else if (a == "-h" || a == "--help") {
      std::cerr << "registry-embed [--force] [--all] [--max N] [--model id]\n"
                   "  Embebe fichas cuyo card_hash cambió. --all incluye glue.\n";
      return 2;
    }
  }
  tuide::EmbeddingBackend backend;
  std::string err;
  if (!ensure_embed_backend(root, &backend, &err)) {
    std::cerr << "registry-embed: " << err << "\n";
    return 1;
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  auto one = [&](bool is_query, const std::string& text, std::vector<float>* out) {
    return is_query ? backend.embed_query(text, out, &err) : backend.embed_passage(text, out, &err);
  };
  auto many = [&](const std::vector<std::string>& texts, std::vector<std::vector<float>>* out) {
    return backend.embed_passages(texts, out, &err);
  };
  tuide::RegistryEmbedReport report;
  const bool ok = tuide::registry_embed_nodes(&reg, one, many, opts, &report, &err);
  tuide::registry_close(&reg);
  if (!ok) {
    std::cerr << "registry-embed: " << err << "\n";
    return 1;
  }
  nlohmann::json j = {{"considered", report.considered},
                      {"embedded", report.embedded},
                      {"skipped_cached", report.skipped_cached},
                      {"skipped_glue", report.skipped_glue},
                      {"skipped_ctrl", report.skipped_ctrl},
                      {"failed", report.failed},
                      {"model", opts.model}};
  std::cout << j.dump(2) << "\n";
  return 0;
}

int run_entityness_probe(const std::string& root, int argc, char** argv) {
  std::string query;
  std::string aliases_path;
  std::string out_path;
  std::string pf_path;
  bool use_attrs = false;
  bool want_embed = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--query" && i + 1 < argc) {
      query = argv[++i];
    } else if (a == "--problem-frame-json" && i + 1 < argc) {
      pf_path = argv[++i];
    } else if (a == "--aliases-json" && i + 1 < argc) {
      aliases_path = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (a == "--card-attrs") {
      use_attrs = true;
      want_embed = true;
    } else if (a == "--embed") {
      want_embed = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "entityness-probe --problem-frame-json F [--query TEXT] [--out F]\n"
                   "  | --query TEXT [--aliases-json F]   # legacy prompt-token probe\n"
                   "  [--embed] [--card-attrs]\n"
                   "  Scores PF chain links (primary/secondary) → explore_mode.\n";
      return 2;
    } else if (!a.empty() && a[0] != '-' && query.empty()) {
      query = a;
    }
  }
  if (pf_path.empty() && query.empty()) {
    std::cerr << "entityness-probe: falta --problem-frame-json o --query\n";
    return 2;
  }
  tuide::EffectRegistry reg;
  std::string err;
  if (!tuide::registry_open(root, &reg, &err)) {
    std::cerr << "entityness-probe: registry_open: " << err << "\n";
    return 1;
  }
  tuide::EntitynessOpts eopts;
  eopts.use_card_attrs = use_attrs;
  if (!aliases_path.empty()) {
    try {
      const auto j = nlohmann::json::parse(read_file(aliases_path));
      if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
          std::vector<std::string> als;
          if (it.value().is_array()) {
            for (const auto& x : it.value()) {
              if (x.is_string()) {
                als.push_back(x.get<std::string>());
              }
            }
          }
          eopts.aliases_by_term[it.key()] = std::move(als);
        }
      }
    } catch (const std::exception& ex) {
      std::cerr << "entityness-probe: aliases-json: " << ex.what() << "\n";
      tuide::registry_close(&reg);
      return 1;
    }
  }
  tuide::RegistryEmbedFn embed;
  tuide::EmbeddingBackend backend;
  if (want_embed) {
    if (!ensure_embed_backend(root, &backend, &err)) {
      std::cerr << "entityness-probe: embed backend: " << err << "\n";
      tuide::registry_close(&reg);
      return 1;
    }
    embed = [&](bool is_query, const std::string& text, std::vector<float>* out) {
      std::string e2;
      return is_query ? backend.embed_query(text, out, &e2) : backend.embed_passage(text, out, &e2);
    };
  }

  nlohmann::json j;
  if (!pf_path.empty()) {
    tuide::ProblemFrame pf;
    std::string perr;
    if (!tuide::problem_frame_from_json_string(read_file(pf_path), &pf, &perr)) {
      std::cerr << "entityness-probe: problem-frame: " << perr << "\n";
      tuide::registry_close(&reg);
      return 1;
    }
    tuide::EntitynessLinkReport report;
    if (!tuide::entityness_score_problem_frame(&reg, pf, query, embed, eopts, &report, &err)) {
      std::cerr << "entityness-probe: " << err << "\n";
      tuide::registry_close(&reg);
      return 1;
    }
    j = report.to_json();
    std::cout << tuide::entityness_links_prompt_block(report);
  } else {
    tuide::EntitynessReport report;
    if (!tuide::entityness_probe(&reg, query, embed, eopts, &report, &err)) {
      std::cerr << "entityness-probe: " << err << "\n";
      tuide::registry_close(&reg);
      return 1;
    }
    j = report.to_json();
  }
  std::cout << j.dump(2) << "\n";
  if (!out_path.empty()) {
    std::ofstream(out_path) << j.dump(2) << "\n";
  }
  tuide::registry_close(&reg);
  return 0;
}

int run_wave_bosquejo(const std::string& root, int argc, char** argv) {
  std::vector<std::string> terms;
  std::string out_path;
  bool debug = false;
  auto push_csv = [&](const std::string& csv) {
    std::string cur;
    for (char c : csv) {
      if (c == ',') {
        const std::string t = trim(cur);
        if (!t.empty()) {
          terms.push_back(t);
        }
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    const std::string t = trim(cur);
    if (!t.empty()) {
      terms.push_back(t);
    }
  };
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if ((a == "--terms" || a == "--term") && i + 1 < argc) {
      push_csv(argv[++i]);
    } else if (a == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (a == "--debug") {
      debug = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "wave-bosquejo --terms \"evento entrada,parada,archivo\" [--out F] [--debug]\n"
                   "  Sin LLM. Olores de zona (1–3 palabras) → foto de barrios (concentración + aristas).\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      push_csv(a);
    }
  }
  if (terms.size() < static_cast<std::size_t>(tuide::kWaveControlBosquejarMin)) {
    std::cerr << "wave-bosquejo: hace falta --terms con 2–8 conceptos\n";
    return 2;
  }
  tuide::EmbeddingBackend backend;
  std::string err;
  tuide::RegistryEmbedFn embed;
  if (ensure_embed_backend(root, &backend, &err)) {
    embed = [&](bool is_query, const std::string& text, std::vector<float>* out) {
      return is_query ? backend.embed_query(text, out, &err) : backend.embed_passage(text, out, &err);
    };
  } else {
    std::cerr << "wave-bosquejo: embed no listo (" << err << "); solo NodeId\n";
    err.clear();
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  tuide::WaveControlBosquejo foto;
  if (!tuide::wave_control_bosquejo_from_registry(&reg, embed, terms, &foto, &err)) {
    std::cerr << "wave-bosquejo: " << err << "\n";
    tuide::registry_close(&reg);
    return 1;
  }
  const std::string md = tuide::wave_control_bosquejo_markdown(foto);
  std::cout << md << "\n";
  if (debug && !foto.detalle.empty()) {
    std::cerr << "detalle:\n" << foto.detalle;
  }
  if (!out_path.empty()) {
    std::ofstream(out_path) << md << "\n";
  }
  tuide::registry_close(&reg);
  return 0;
}

int run_catalog(const std::string& root, int argc, char** argv) {
  const std::string cmd = argc > 1 ? argv[1] : "catalog-plano";
  bool force = false;
  std::string query;
  std::string zoom_id;
  std::vector<std::string> rest;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--force") {
      force = true;
    } else if ((a == "--query" || a == "-q") && i + 1 < argc) {
      query = argv[++i];
    } else if (a == "-h" || a == "--help") {
      std::cerr << "catalog-build [--force]\n"
                   "catalog-plano [--query TEXT]\n"
                   "catalog-zoom ID [--query TEXT]\n"
                   "catalog-search QUERY\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      rest.push_back(a);
    }
  }
  if (cmd == "catalog-zoom" && zoom_id.empty() && !rest.empty()) {
    zoom_id = rest.front();
  }
  if (cmd == "catalog-search" && query.empty() && !rest.empty()) {
    query = rest.front();
  }
  tuide::Catalog cat;
  std::string err;
  if (!tuide::catalog_ensure(root, &cat, &err, force)) {
    std::cerr << "catalog: " << err << "\n";
    return 1;
  }
  if (cmd == "catalog-build") {
    std::cout << "catalog " << (cat.cache_hit ? "hit" : "rebuild") << " censo=" << cat.censo
              << " barrios=" << cat.barrios.size() << " stems=" << cat.stems.size()
              << " cards=" << cat.cards.size() << "\n";
    return 0;
  }
  tuide::catalog_apply_heat(&cat, query);
  if (cmd == "catalog-plano") {
    std::cout << tuide::catalog_render_plano(cat);
    return 0;
  }
  if (cmd == "catalog-zoom") {
    if (zoom_id.empty()) {
      std::cerr << "catalog-zoom: falta ID\n";
      return 2;
    }
    const std::string md = tuide::catalog_render_zoom(cat, zoom_id);
    if (md.empty()) {
      std::cerr << "catalog-zoom: id no está en el plano\n";
      return 1;
    }
    std::cout << md;
    return 0;
  }
  if (cmd == "catalog-search") {
    if (query.empty()) {
      std::cerr << "catalog-search: falta QUERY\n";
      return 2;
    }
    std::cout << tuide::catalog_render_search(cat);
    return 0;
  }
  std::cerr << "catalog: subcomando build|plano|zoom|search\n";
  return 2;
}

int run_registry_query(const std::string& root, int argc, char** argv) {
  std::string query;
  tuide::RegistryQueryOpts opts;
  bool want_code = false;
  bool want_trails = false;
  bool want_judge_cards = false;
  bool judge_cards_markdown = false;
  std::string judge_cards_json_out;
  std::string judge_knobs_path;
  std::string map_path;
  int map_top = tuide::kRegistryQueryMapStems;
  bool view_set = false;
  tuide::GraphViewLevel view_level = tuide::GraphViewLevel::Inspect;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--top" && i + 1 < argc) {
      opts.top_k = std::atoi(argv[++i]);
    } else if (a == "--hops" && i + 1 < argc) {
      opts.hops = std::atoi(argv[++i]);
    } else if (a == "--kind" && i + 1 < argc) {
      opts.hop_kinds = parse_seeds_csv(argv[++i]);
    } else if (a == "--model" && i + 1 < argc) {
      opts.model = argv[++i];
    } else if (a == "--threads" && i + 1 < argc) {
      opts.threads = std::atoi(argv[++i]);
    } else if (a == "--map" && i + 1 < argc) {
      map_path = argv[++i];
    } else if (a == "--map-top" && i + 1 < argc) {
      map_top = std::atoi(argv[++i]);
    } else if (a == "--trails") {
      want_trails = true;
    } else if (a == "--judge-cards") {
      want_trails = true;
      want_judge_cards = true;
    } else if (a == "--judge-cards-md") {
      want_trails = true;
      want_judge_cards = true;
      judge_cards_markdown = true;
    } else if (a == "--judge-cards-json-out" && i + 1 < argc) {
      judge_cards_json_out = argv[++i];
    } else if (a == "--judge-knobs" && i + 1 < argc) {
      judge_knobs_path = argv[++i];
    } else if (a == "--view" && i + 1 < argc) {
      if (!tuide::graph_view_level_parse(argv[++i], &view_level)) {
        std::cerr << "registry-query: --view inválido (atlas|inspect|deep)\n";
        return 2;
      }
      view_set = true;
    } else if (a == "--code") {
      want_code = true;
    } else if (a == "--match-surface" && i + 1 < argc) {
      if (!tuide::registry_match_surface_parse(argv[++i], &opts.match_surface)) {
        std::cerr << "registry-query: --match-surface inválido (card_full|latch|card_attrs|node_id)\n";
        return 2;
      }
    } else if (a == "-h" || a == "--help") {
      std::cerr << "registry-query TEXT [--top K] [--hops N] [--kind call,write] [--trails]\n"
                   "  [--threads N] [--map map.md] [--map-top N] [--code]\n"
                   "  [--match-surface card_full|latch|card_attrs|node_id]\n"
                   "  [--judge-cards|--judge-cards-md] [--judge-cards-json-out FILE]\n"
                   "  [--judge-knobs FILE.json] [--view atlas|inspect|deep]\n"
                   "  cosine + hops. --trails: PPR + beam + constelaciones. "
                   "--map: items L1 (símbolo) ocupan hop0. --view recorta el pack (atlas=ancho).\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      if (!query.empty()) {
        query += " ";
      }
      query += a;
    }
  }
  if (query.empty()) {
    std::cerr << "registry-query: falta TEXT\n";
    return 2;
  }
  if (!map_path.empty()) {
    fs::path mp(map_path);
    if (!mp.is_absolute()) {
      mp = fs::path(root) / map_path;
    }
    const std::string md = read_file(mp);
    tuide::EffectSliceSeedIn in;
    tuide::effect_slice_fill_seed_from_map(&in, md, map_top > 0 ? map_top : tuide::kRegistryQueryMapStems);
    std::vector<std::string> stems;
    std::unordered_set<std::string> seen_stems;
    for (const auto& fn : in.map_window) {
      if (fn.file_level || fn.symbol.empty() || fn.path.empty()) {
        continue;
      }
      tuide::RegistryBoostFn bf;
      bf.path = fn.path;
      bf.symbol = fn.symbol;
      opts.boost_fns.push_back(std::move(bf));
      const std::string st = tuide::registry_stem_of(fn.path);
      if (!st.empty()) {
        seen_stems.insert(st);
      }
    }
    for (const auto& p : in.inventory_paths) {
      const std::string st = tuide::registry_stem_of(p);
      if (st.empty() || !seen_stems.insert(st).second) {
        continue;
      }
      stems.push_back(st);
    }
    opts.boost_stems = std::move(stems);
  }
  tuide::EmbeddingBackend backend;
  std::string err;
  if (!ensure_embed_backend(root, &backend, &err)) {
    std::cerr << "registry-query: " << err << "\n";
    return 1;
  }
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }
  auto one = [&](bool is_query, const std::string& text, std::vector<float>* out) {
    return is_query ? backend.embed_query(text, out, &err) : backend.embed_passage(text, out, &err);
  };
  if (want_trails) {
    tuide::RegistryTrailResult res;
    const bool ok = tuide::registry_query_trails(&reg, query, one, opts, &res, &err);
    nlohmann::json judge_payload;
    bool judge_ok = true;
    if (ok && want_judge_cards) {
      tuide::RegistryCausalJudgeOpts judge_opts;
      judge_opts.max_zones = 8;
      judge_opts.promote_uncovered = true;
      if (view_set) {
        tuide::graph_view_profile_apply(tuide::graph_view_profile_default(view_level),
                                        &judge_opts);
      }
      if (!judge_knobs_path.empty()) {
        fs::path knobs_path(judge_knobs_path);
        if (!knobs_path.is_absolute()) {
          knobs_path = fs::path(root) / knobs_path;
        }
        std::ifstream knobs_in(knobs_path);
        if (!knobs_in) {
          std::cerr << "registry-query: no se pudo abrir --judge-knobs " << knobs_path << "\n";
          return 1;
        }
        nlohmann::json knobs_json;
        try {
          knobs_in >> knobs_json;
        } catch (const std::exception& ex) {
          std::cerr << "registry-query: judge-knobs JSON inválido: " << ex.what() << "\n";
          return 1;
        }
        if (!tuide::registry_causal_judge_opts_apply_json(&judge_opts, knobs_json, &err)) {
          std::cerr << "registry-query: judge-knobs: " << err << "\n";
          return 1;
        }
      }
      judge_ok =
          tuide::registry_causal_judge_payload(&reg, query, res, judge_opts, &judge_payload, &err);
    }
    tuide::registry_close(&reg);
    if (!ok) {
      std::cerr << "registry-query: " << err << "\n";
      return 1;
    }
    if (!judge_ok) {
      std::cerr << "registry-query judge cards: " << err << "\n";
      return 1;
    }
    if (want_judge_cards) {
      if (!judge_cards_json_out.empty()) {
        fs::path json_out(judge_cards_json_out);
        if (!json_out.is_absolute()) {
          json_out = fs::path(root) / json_out;
        }
        std::ofstream(json_out) << judge_payload.dump(2) << "\n";
      }
      if (judge_cards_markdown) {
        if (view_set) {
          std::cout << tuide::registry_causal_pack_markdown(judge_payload, view_level);
        } else {
          std::cout << tuide::registry_causal_judge_markdown(judge_payload);
        }
      } else {
        std::cout << judge_payload.dump(2) << "\n";
      }
      return 0;
    }
    nlohmann::json trails = nlohmann::json::array();
    for (const auto& t : res.trails) {
      nlohmann::json hops = nlohmann::json::array();
      for (const auto& h : t.hops) {
        hops.push_back({{"id", h.node.id},
                        {"kind", h.node.kind},
                        {"symbol", h.node.symbol},
                        {"path", h.node.path},
                        {"stem", h.node.stem},
                        {"mass", h.mass},
                        {"cosine", h.cosine},
                        {"cond", h.node.cond}});
      }
      trails.push_back({{"id", t.id},
                        {"score", t.score},
                        {"why", t.why},
                        {"latches", t.latches},
                        {"hops", hops}});
    }
    nlohmann::json constellations = nlohmann::json::array();
    for (const auto& c : res.constellations) {
      nlohmann::json nodes = nlohmann::json::array();
      for (const auto& h : c.nodes) {
        nodes.push_back({{"id", h.node.id},
                         {"kind", h.node.kind},
                         {"symbol", h.node.symbol},
                         {"path", h.node.path},
                         {"stem", h.node.stem},
                         {"mass", h.mass},
                         {"cosine", h.cosine},
                         {"cond", h.node.cond}});
      }
      constellations.push_back({{"id", c.id},
                                {"center_id", c.center_id},
                                {"member", c.member},
                                {"score", c.score},
                                {"mass_coverage", c.mass_coverage},
                                {"why", c.why},
                                {"core_stems", c.core_stems},
                                {"context_stems", c.context_stems},
                                {"primary_stems", c.primary_stems},
                                {"peripheral_stems", c.peripheral_stems},
                                {"writers", c.writers},
                                {"readers", c.readers},
                                {"controls", c.controls},
                                {"handoffs", c.handoffs},
                                {"nodes", nodes}});
    }
    nlohmann::json macro_constellations = nlohmann::json::array();
    for (const auto& m : res.macro_constellations) {
      nlohmann::json nodes = nlohmann::json::array();
      for (const auto& h : m.nodes) {
        nodes.push_back({{"id", h.node.id},
                         {"kind", h.node.kind},
                         {"symbol", h.node.symbol},
                         {"path", h.node.path},
                         {"stem", h.node.stem},
                         {"mass", h.mass},
                         {"cosine", h.cosine},
                         {"cond", h.node.cond}});
      }
      macro_constellations.push_back({{"id", m.id},
                                      {"score", m.score},
                                      {"mass_coverage", m.mass_coverage},
                                      {"why", m.why},
                                      {"nuclei", m.nuclei},
                                      {"anchor_groups", m.anchor_groups},
                                      {"primary_stems", m.primary_stems},
                                      {"merge_witnesses", m.merge_witnesses},
                                      {"merge_strength", m.merge_strength},
                                      {"nodes", nodes}});
    }
    nlohmann::json seeds = nlohmann::json::array();
    for (const auto& h : res.seeds) {
      seeds.push_back({{"id", h.node.id},
                       {"kind", h.node.kind},
                       {"symbol", h.node.symbol},
                       {"path", h.node.path},
                       {"stem", h.node.stem},
                       {"mass", h.mass},
                       {"cosine", h.cosine}});
    }
    nlohmann::json j = {{"query", query},
                        {"seeds", seeds},
                        {"trails", trails},
                        {"constellations", constellations},
                        {"macro_constellations", macro_constellations},
                        {"holes", res.holes},
                        {"subgraph_nodes", res.subgraph_nodes},
                        {"subgraph_facts", res.subgraph_facts},
                        {"max_cosine", res.max_cosine},
                        {"map_boosted", res.map_boosted},
                        {"weak_gate", res.weak_gate}};
    std::cout << j.dump(2) << "\n";
    return res.trails.empty() && res.constellations.empty() ? 1 : 0;
  }
  tuide::RegistryQueryResult res;
  const bool ok = tuide::registry_query(&reg, query, one, opts, &res, &err);
  if (!ok) {
    tuide::registry_close(&reg);
    std::cerr << "registry-query: " << err << "\n";
    return 1;
  }
  nlohmann::json hits = nlohmann::json::array();
  for (const auto& h : res.hits) {
    nlohmann::json row = tuide::registry_node_to_json(h.node);
    row["cosine"] = h.cosine;
    row["hop"] = h.hop;
    hits.push_back(std::move(row));
  }
  nlohmann::json expanded = nlohmann::json::array();
  for (const auto& h : res.expanded) {
    nlohmann::json row = tuide::registry_node_to_json(h.node);
    row["cosine"] = h.cosine;
    row["hop"] = h.hop;
    expanded.push_back(std::move(row));
  }
  nlohmann::json j = {{"query", query}, {"hits", hits}, {"expanded", expanded}};
  std::cout << j.dump(2) << "\n";
  if (want_code) {
    for (const auto& h : res.hits) {
      if (h.node.kind != "fn" || h.node.path.empty()) {
        continue;
      }
      tuide::GetCodeOfRequest req;
      req.workspace_root = root;
      req.file = h.node.path;
      req.symbol = h.node.symbol;
      req.line = h.node.line;
      const auto got = tuide::get_code_of(req);
      std::cout << "\n======== code " << h.node.id << " ========\n";
      if (got.ok) {
        std::cout << tuide::format_get_code_of_result(got, h.node.path);
      } else {
        std::cout << got.error << "\n";
      }
    }
  }
  tuide::registry_close(&reg);
  return res.hits.empty() ? 1 : 0;
}

int run_zone_judge_shot(const std::string& root, int argc, char** argv) {
  std::string cards_path;
  std::string instruction;
  std::string case_id;
  std::string out_dir;
  std::string expect_zone;
  std::string model_id;
  bool dry = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--cards" && i + 1 < argc) {
      cards_path = argv[++i];
    } else if (a == "--instruction" && i + 1 < argc) {
      instruction = argv[++i];
    } else if (a == "--case" && i + 1 < argc) {
      case_id = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (a == "--expect" && i + 1 < argc) {
      expect_zone = argv[++i];
    } else if (a == "--model-id" && i + 1 < argc) {
      model_id = argv[++i];
    } else if (a == "--dry") {
      dry = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "zone-judge-shot --cards FILE (--case ID|--instruction TEXT)\n"
                   "                [--out DIR] [--expect M1] [--dry]\n"
                   "  Una llamada LLM sobre fichas causal_judge_v1; conserva prompt y decisión.\n";
      return 2;
    }
  }
  if (!case_id.empty() && instruction.empty()) {
    const fs::path prompts =
        fs::path(root) / "tests/fixtures/stem_boost_battery/prompts_nl_human.json";
    std::ifstream in(prompts);
    nlohmann::json arr;
    if (!in) {
      std::cerr << "zone-judge-shot: no se pudo leer " << prompts << "\n";
      return 2;
    }
    in >> arr;
    for (const auto& item : arr) {
      if (item.value("id", "") == case_id) {
        instruction = item.value("prompt", "");
        break;
      }
    }
  }
  if (cards_path.empty() || instruction.empty()) {
    std::cerr << "zone-judge-shot: requiere --cards y --case/--instruction\n";
    return 2;
  }
  fs::path cards_file(cards_path);
  if (!cards_file.is_absolute()) {
    cards_file = fs::path(root) / cards_file;
  }
  const std::string cards = read_file(cards_file);
  const auto zone_ids = tuide::registry_causal_judge_zone_ids(cards);
  if (cards.empty() || zone_ids.empty()) {
    std::cerr << "zone-judge-shot: fichas vacías o sin ids M*: " << cards_file << "\n";
    return 2;
  }
  const std::string system = tuide::registry_causal_judge_system_prompt();
  const std::string user =
      "## Consulta\n" + instruction + "\n\n" + tuide::registry_causal_judge_user_prompt(cards);
  if (!out_dir.empty()) {
    fs::path output(out_dir);
    if (!output.is_absolute()) {
      output = fs::path(root) / output;
    }
    fs::create_directories(output);
    std::ofstream(output / "system.txt") << system;
    std::ofstream(output / "user.md") << user;
    std::ofstream(output / "cards.md") << cards;
    out_dir = output.string();
  }
  std::cout << "======== zone-judge-shot ========\n"
            << "case=" << (case_id.empty() ? "custom" : case_id) << " zones=";
  for (std::size_t i = 0; i < zone_ids.size(); ++i) {
    std::cout << (i ? "," : "") << zone_ids[i];
  }
  std::cout << " prompt_chars=" << system.size() + user.size() << "\n";
  if (dry) {
    std::cout << user << "\ndry: no LLM\n";
    return 0;
  }

  AiSettings settings = load_ai_settings(root);
  if (!model_id.empty()) {
    settings.level2.model_id = model_id;
    settings.level2.model_path.clear();
  }
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  auto brain_ptr = ready_l2_brain(settings, progress, &err);
  if (!brain_ptr) {
    std::cerr << "zone-judge-shot: " << err << "\n";
    return err.find("mode") != std::string::npos ? 2 : 1;
  }
  tuide::L2Brain& brain = *brain_ptr;
  tuide::L2BrainRequest request;
  request.system_prompt = system;
  request.user_prompt = user;
  request.phase = "causal_zone_judge";
  request.max_tokens =
      std::min(384, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 384);
  request.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
  request.temperature = 0.05f;
  std::cout << "L2 ▸ zone-judge-shot (" << brain.name() << ")…\n";
  const auto response = tuide::propose_with_think(brain, &request, nullptr);
  if (!response.ok) {
    std::cerr << "LLM FAIL: " << response.error << "\n";
    return 1;
  }
  auto decision =
      tuide::registry_parse_causal_judge_decision(response.text, zone_ids);
  if (decision.ok) {
    for (const auto& verdict : decision.zones) {
      for (const auto& symbol : verdict.expand_from) {
        if (cards.find(symbol) == std::string::npos) {
          decision.ok = false;
          decision.error = "expand_from no pertenece a la ficha " + verdict.id + ": " + symbol;
          break;
        }
      }
      if (!decision.ok) {
        break;
      }
    }
  }
  const nlohmann::json decision_json =
      tuide::registry_causal_judge_decision_to_json(decision);
  if (!out_dir.empty()) {
    std::ofstream(fs::path(out_dir) / "model_raw.txt") << response.text;
    std::ofstream(fs::path(out_dir) / "decision.json") << decision_json.dump(2) << "\n";
  }
  std::cout << "\n--- model ---\n" << response.text << "\n--- decision ---\n"
            << decision_json.dump(2) << "\n";
  if (!decision.ok) {
    return 1;
  }
  if (!expect_zone.empty()) {
    const bool hit =
        std::find(decision.selected.begin(), decision.selected.end(), expect_zone) !=
        decision.selected.end();
    std::cout << "EXPECT " << expect_zone << ": " << (hit ? "PASS" : "FAIL") << "\n";
    return hit ? 0 : 1;
  }
  return 0;
}

std::unordered_map<std::string, std::vector<std::string>> triage_targets_by_zone(
    const nlohmann::json& payload) {
  std::unordered_map<std::string, std::vector<std::string>> out;
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    const std::string id = zone.value("id", "");
    if (id.empty()) {
      continue;
    }
    std::unordered_set<std::string> seen;
    std::function<void(const nlohmann::json&)> visit = [&](const nlohmann::json& value) {
      if (value.is_object()) {
        if (value.contains("target") && value["target"].is_string()) {
          const std::string target = value["target"].get<std::string>();
          if (!target.empty() && seen.insert(target).second) {
            out[id].push_back(target);
          }
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
          visit(it.value());
        }
      } else if (value.is_array()) {
        for (const auto& item : value) {
          visit(item);
        }
      }
    };
    visit(zone);
  }
  return out;
}

std::vector<std::string> zone_ids_from_payload(const nlohmann::json& payload) {
  std::vector<std::string> out;
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    const std::string id = zone.value("id", "");
    if (!id.empty()) {
      out.push_back(id);
    }
  }
  return out;
}

bool target_matches_query(const std::string& target, const std::string& query) {
  std::string lower_query = query;
  std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto colon = target.rfind(':');
  std::string symbol = colon == std::string::npos ? target : target.substr(colon + 1);
  std::string token;
  for (char c : symbol + "_") {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      continue;
    }
    if (token.size() >= 4 && lower_query.find(token) != std::string::npos) {
      return true;
    }
    token.clear();
  }
  return false;
}

void append_outline_symbols(tuide::RegistryCausalPilotWorkerNotebook* nb, const std::string& path,
                            const std::string& body) {
  if (nb == nullptr) {
    return;
  }
  tuide::registry_causal_pilot_notebook_add_target(nb, path);
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 5 && std::isalpha(static_cast<unsigned char>(cur[0]))) {
      tuide::registry_causal_pilot_notebook_add_target(nb, path + ":" + cur);
    }
    cur.clear();
  };
  for (char c : body) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      cur.push_back(c);
    } else {
      flush();
    }
  }
  flush();
}

tuide::RegistryCausalPilotWorkerReport run_one_pilot_worker(
    tuide::L2Brain& brain, const AiSettings& settings, const std::string& root,
    const fs::path& case_out, int task_i, const tuide::RegistryCausalPilotTask& task,
    const nlohmann::json& inspect_payload, const nlohmann::json& atlas_payload,
    tuide::GraphViewLevel view, const std::string& instruction) {
  tuide::RegistryCausalPilotWorkerReport report;
  report.kind = task.kind;
  report.zone = task.zone;
  report.stem = task.stem;
  nlohmann::json one =
      tuide::registry_causal_payload_filter_zones(inspect_payload, {task.zone});
  if (one.value("zones", nlohmann::json::array()).empty()) {
    one = tuide::registry_causal_payload_filter_zones(atlas_payload, {task.zone});
    view = tuide::GraphViewLevel::Atlas;
  }
  one["uncovered_seeds"] = nlohmann::json::array();
  const std::string zone_md = tuide::registry_causal_pack_markdown(one, view);
  tuide::RegistryCausalPilotWorkerNotebook nb;
  tuide::registry_causal_pilot_notebook_from_payload(one, task.stem, &nb);
  const std::string prefix = "w" + std::to_string(task_i);
  std::ofstream(case_out / (prefix + "_zone.md")) << zone_md;
  int step_limit = tuide::kPilotWorkerMaxSteps;
  for (int step = 0; step < step_limit; ++step) {
    const bool last_turn = step >= tuide::kPilotWorkerMaxSteps - 1;
    if (last_turn && nb.notes.find("----- cierre -----") == std::string::npos) {
      nb.notes +=
          "\n----- cierre -----\nÚltimo turno: emite causal_pilot_worker_v1 con walk (duda:Symbol "
          "si queda un hueco). Si no entiendes, verdict no_cubre o missing. PROHIBIDO tool.\n";
    }
    tuide::L2BrainRequest req;
    req.system_prompt = tuide::registry_causal_pilot_worker_system_prompt(task.kind, task.stem);
    req.user_prompt =
        "## Consulta del usuario\n" + instruction + "\n\n## Encargo del piloto\n" +
        task.question + "\n\n" +
        tuide::registry_causal_pilot_worker_user_prompt(
            task.kind, task.question, zone_md, nb.notes,
            tuide::registry_causal_pilot_allowed_markdown(nb), last_turn, task.stem);
    req.phase = "causal_pilot_worker";
    req.max_tokens =
        std::min(640, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 640);
    req.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
    req.temperature = 0.05f;
    if (step == 0) {
      std::ofstream(case_out / (prefix + "_system.txt")) << req.system_prompt;
    }
    std::ofstream(case_out / (prefix + "_user_" + std::to_string(step) + ".md"))
        << req.user_prompt;
    const auto response = tuide::propose_with_think(brain, &req, nullptr);
    std::ofstream(case_out / (prefix + "_raw_" + std::to_string(step) + ".txt"))
        << response.text;
    if (!response.ok) {
      report.error = response.error;
      report.raw = response.text;
      report.steps = step + 1;
      return report;
    }
    report = tuide::registry_parse_causal_pilot_worker(response.text, task.kind, task.stem,
                                                       instruction, &nb);
    report.zone = task.zone;
    report.stem = task.stem;
    report.steps = step + 1;
    if (report.ok && !report.is_tool) {
      int zone_ov = 0;
      const auto& zs = one.value("zones", nlohmann::json::array());
      if (!zs.empty() && zs.front().is_object()) {
        zone_ov = tuide::registry_causal_query_zone_overlap(instruction, zs.front());
      }
      const bool cubre_mismatch =
          task.kind == "cubre" && report.verdict == "cubre" && !instruction.empty() &&
          tuide::registry_causal_query_hay_overlap(
              instruction, report.why + " " + report.owns + " " + report.stem) <= 0;
      const bool card_miss = zone_ov <= 0;
      if (cubre_mismatch && card_miss && nb.n_query_nudge < 1 && !last_turn) {
        ++nb.n_query_nudge;
        std::ostringstream note;
        note << "\n----- nota -----\nEl símbolo de why no solapa con ## Consulta del usuario. "
                "Si este barrio implementa otro objeto, emite informe verdict=no_cubre citando "
                "un símbolo de la ficha.\n";
        nb.notes += note.str();
        std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
            << note.str();
        continue;
      }
      if (cubre_mismatch && card_miss) {
        report.verdict = "no_cubre";
        report.covers = false;
      }
      return report;
    }
    const bool missing_tool_err =
        report.error == "sin lectura de código" ||
        report.error == "sin dataflow ni diagrama causal" ||
        report.error == "ficha vacía: pide outline o need_code" ||
        report.error == "follow sin target" || report.error == "need_code sin target" ||
        report.error == "outline sin target" || report.error == "dataflow sin target" ||
        report.error == "causal sin target";
    const bool ficha_tool_err =
        report.error == "follow no está en la ficha" ||
        report.error == "need_code no está en la ficha" ||
        report.error == "causal no está en la ficha" ||
        report.error == "follow fuera del stem" || report.error == "need_code fuera del stem" ||
        report.error == "dataflow fuera del stem" || report.error == "causal fuera del stem";
    const bool empty_surrender = report.error == "follow vacío no es no_cubre";
    const bool brief_corto = report.error == "brief corto: 2 frases para el piloto";
    const bool no_cite = report.error == "why no cita un símbolo de la ficha";
    const bool walk_unread =
        report.error.find("walk nombra un símbolo no leído") == 0;
    const bool walk_no_flow = report.error.find("falta dataflow del campo") == 0;
    const bool walk_no_writer = report.error.find("falta need_code del writer") == 0;
    const bool walk_template = report.error == "walk de plantilla";
    const bool tool_repeat = report.error == "need_code repetido" ||
                             report.error == "dataflow repetido";
    if (!report.is_tool && missing_tool_err && !last_turn) {
      std::ostringstream note;
      note << "\n----- nota -----\nInforme rechazado: " << report.error << ". ";
      if (report.error == "sin lectura de código") {
        note << "PROHIBIDO causal_pilot_worker_v1. Pide ahora SOLO una tool";
        if (!nb.allowed_targets.empty()) {
          note << ": {\"action\":\"causal_pilot_need_code\",\"target\":\"" << nb.allowed_targets.front()
               << "\"}";
        } else {
          note << " (causal_pilot_need_code o outline).";
        }
        note << " Follow no es lectura.\n";
      } else {
        note << "Pide need_code u otra tool del catálogo. Follow no es lectura.\n";
      }
      nb.notes += note.str();
      std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
          << note.str();
      continue;
    }
    if (!report.is_tool && tool_repeat && !last_turn) {
      std::ostringstream note;
      note << "\n----- nota -----\nInforme rechazado: " << report.error
           << ". Ya está en ## Notas. Emite causal_pilot_worker_v1 ahora "
              "(cubre|no_cubre|chain|missing). PROHIBIDO repetir esa tool.\n";
      nb.notes += note.str();
      std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
          << note.str();
      continue;
    }
    if (!report.is_tool && ficha_tool_err && !last_turn) {
      std::ostringstream note;
      note << "\n----- nota -----\nInforme rechazado: " << report.error
           << ". Elige un target de ## Símbolos permitidos; no inventes.";
      if (!nb.allowed_targets.empty()) {
        note << " Ejemplo: {\"action\":\"causal_pilot_need_code\",\"target\":\""
             << nb.allowed_targets.front() << "\"}";
      }
      note << "\n";
      nb.notes += note.str();
      std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
          << note.str();
      continue;
    }
    if (!report.is_tool && empty_surrender && !last_turn) {
      std::ostringstream note;
      note << "\n----- nota -----\nInforme rechazado: " << report.error
           << ". Follow sin hops no autoriza no_cubre. outline del archivo del stem, o "
              "follow/need_code de OTRO símbolo de ## Símbolos permitidos, o chain si el "
              "símbolo ES el acto.\n";
      nb.notes += note.str();
      std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
          << note.str();
      continue;
    }
    if (!report.is_tool && no_cite && !last_turn) {
      std::ostringstream note;
      note << "\n----- nota -----\nInforme rechazado: " << report.error
           << ". Reemite causal_pilot_worker_v1 con why/brief que nombren un símbolo de "
              "## Símbolos permitidos (el que leíste), no solo el tema de la consulta.\n";
      nb.notes += note.str();
      std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
          << note.str();
      continue;
    }
    if (!report.is_tool && (walk_unread || walk_template || walk_no_flow || walk_no_writer) &&
        !last_turn) {
      std::ostringstream note;
      note << "\n----- nota -----\nInforme rechazado: " << report.error
           << ". PROHIBIDO reemitir el mismo walk. Pide ahora SOLO una tool";
      const auto colon = report.error.find(": ");
      if (walk_no_flow && colon != std::string::npos) {
        note << ": {\"action\":\"causal_pilot_dataflow\",\"target\":\""
             << report.error.substr(colon + 2) << "\"}";
      } else if ((walk_unread || walk_no_writer) && colon != std::string::npos) {
        note << ": {\"action\":\"causal_pilot_need_code\",\"target\":\""
             << report.error.substr(colon + 2) << "\"}";
      } else {
        note << " (need_code de ese símbolo o dataflow del campo)";
      }
      note << ".\n";
      nb.notes += note.str();
      std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
          << note.str();
      continue;
    }
    if (!report.is_tool && brief_corto && nb.n_query_nudge < 1 && !last_turn) {
      ++nb.n_query_nudge;
      std::ostringstream note;
      note << "\n----- nota -----\nInforme rechazado: " << report.error
           << ". Reemite causal_pilot_worker_v1 con brief de 2 frases para el piloto, "
              "citando un símbolo de la ficha.\n";
      nb.notes += note.str();
      std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
          << note.str();
      continue;
    }
    const bool bad_como_verdict = task.kind == "como" && !report.is_tool &&
                                  (report.error == "verdict como inválido" ||
                                   report.verdict == "need_code" || report.verdict == "follow" ||
                                   report.verdict == "outline" || report.verdict == "dataflow" ||
                                   report.verdict == "causal");
    if (bad_como_verdict && !last_turn) {
      std::ostringstream note;
      note << "\n----- nota -----\nInforme rechazado: verdict de como debe ser chain o "
              "no_cubre, no need_code. Reemite causal_pilot_worker_v1 con chain/why/brief.\n";
      nb.notes += note.str();
      std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
          << note.str();
      continue;
    }
    if (report.ok && report.is_tool && last_turn) {
      if (step_limit == tuide::kPilotWorkerMaxSteps) {
        step_limit = tuide::kPilotWorkerMaxSteps + 1;
        std::ostringstream note;
        note << "\n----- cierre -----\nTool rechazada en el último turno. Emite "
                "causal_pilot_worker_v1; si no entiendes, no_cubre/missing y brief de qué "
                "falta.\n";
        nb.notes += note.str();
        std::ofstream(case_out / (prefix + "_nudge_" + std::to_string(step) + ".md"))
            << note.str();
        continue;
      }
      report.ok = false;
      report.error = "sin informe";
      report.is_tool = false;
      report.need_code = false;
      return report;
    }
    if (!report.ok || !report.is_tool) {
      return report;
    }
    std::ostringstream chunk;
    chunk << "\n----- " << report.tool << " " << report.target;
    if (!report.direction.empty()) {
      chunk << " " << report.direction;
    }
    if (report.tool == "dataflow" && !report.path_symbol.empty()) {
      chunk << " " << report.path_symbol;
    }
    chunk << " -----\n";
    auto search_fn = [&](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
      return harness_rg_symbol(root, symbol);
    };
    if (report.tool == "need_code") {
      ++nb.n_need_code;
      GetCodeOfRequest creq = parse_get_code_of_arg(report.target, root);
      creq.workspace_root = root;
      creq.max_lines = 120;
      const auto got = get_code_of(creq);
      if (got.ok) {
        chunk << tuide::format_get_code_of_header(got, got.path);
        chunk << tuide::wrap_source_fence(got.text, got.path.empty() ? report.target : got.path);
        tuide::registry_causal_pilot_notebook_add_target(&nb, report.target);
        const std::string focus_path = creq.file.empty() ? got.path : creq.file;
        const std::string focus_sym =
            !creq.symbol.empty() ? creq.symbol : (!got.name.empty() ? got.name : report.target);
        const std::string display =
            focus_path.empty() ? focus_sym : (focus_path + ":" + focus_sym);
        chunk << tuide::registry_causal_pilot_aguas_arriba_markdown(root, focus_path, focus_sym,
                                                                    search_fn);
        chunk << tuide::registry_causal_pilot_aguas_abajo_markdown(display, got.text,
                                                                   got.truncated);
      } else {
        chunk << "(get_code_of falló: " << got.error << ")\n";
      }
    } else if (report.tool == "outline") {
      ++nb.n_outline;
      fs::path abs = report.target;
      if (!abs.is_absolute()) {
        abs = fs::path(root) / report.target;
      }
      abs = abs.lexically_normal();
      const std::string cmd = harness_rg_cmd(
          "-n --no-heading -e "
          "'^[a-zA-Z_].*\\(.*\\)\\s*\\{?\\s*$|^\\s*(class|struct|namespace|enum)\\s+' " +
          shell_quote(abs.string()) + " | head -n 80");
      std::string body = (fs::exists(abs) && !cmd.empty()) ? run_cmd(cmd) : std::string{};
      if (body.empty()) {
        body = "(sin outline)\n";
      }
      chunk << "outline: " << report.target << "\n" << body;
      append_outline_symbols(&nb, report.target, body);
    } else if (report.tool == "follow") {
      ++nb.n_follow;
      std::vector<std::string> extra;
      std::string port;
      chunk << tuide::registry_causal_pilot_follow_markdown(one, report.target, report.direction,
                                                            task.stem, &extra, &port);
      for (const auto& t : extra) {
        tuide::registry_causal_pilot_notebook_add_target(&nb, t);
      }
      if (!port.empty()) {
        chunk << "port_to_hint: " << port << "\n";
      }
    } else if (report.tool == "dataflow") {
      ++nb.n_dataflow;
      const auto df = tuide::a_dataflow_build_with_search(root, report.target, report.path_symbol,
                                                         search_fn);
      chunk << tuide::a_dataflow_markdown(df);
      std::vector<std::string> stem_writers;
      auto note_site = [&](const tuide::ADataFlowSite& s, const char* polarity) {
        fs::path abs = fs::path(root) / s.path;
        const auto hop =
            tuide::a_trail_enrich_hop(abs.lexically_normal().string(), s.path, s.line, "");
        if (hop.symbol.empty()) {
          return;
        }
        const std::string target = s.path + ":" + hop.symbol;
        chunk << "fn=`" << target << "` @" << s.line << " " << polarity << "\n";
        const bool same_stem = tuide::registry_causal_pilot_target_in_stem(s.path, task.stem) ||
                               tuide::registry_causal_pilot_target_in_stem(target, task.stem);
        if (same_stem) {
          tuide::registry_causal_pilot_notebook_add_target(&nb, target);
        }
        if (same_stem && polarity != nullptr && polarity[0] == 'w') {
          stem_writers.push_back(target);
        }
      };
      for (const auto& s : df.decls) {
        note_site(s, "decl");
      }
      for (const auto& s : df.writes) {
        note_site(s, "write");
      }
      for (const auto& s : df.reads) {
        note_site(s, "read");
      }
      if (!stem_writers.empty()) {
        chunk << "writers de este stem:";
        for (const auto& w : stem_writers) {
          chunk << " `" << w << "`";
        }
        chunk << ". Un fn= write no está en el walk hasta ----- need_code ----- de ese "
                 "símbolo. Un lector no es el clear.\n";
      }
    } else if (report.tool == "causal") {
      ++nb.n_causal;
      GetCodeOfRequest creq = parse_get_code_of_arg(report.target, root);
      const std::string focus_path = creq.file;
      const std::string focus_sym = creq.symbol.empty() ? report.target : creq.symbol;
      std::vector<tuide::WaveHit> hops;
      chunk << harness_causal_flow_markdown(root, focus_path, focus_sym, search_fn, &hops);
      for (const auto& h : hops) {
        if (!h.path.empty() && !h.symbol.empty()) {
          tuide::registry_causal_pilot_notebook_add_target(&nb, h.path + ":" + h.symbol);
        } else if (!h.id.empty()) {
          tuide::registry_causal_pilot_notebook_add_target(&nb, h.id);
        }
      }
    } else {
      chunk << "(tool desconocida)\n";
    }
    nb.notes += chunk.str();
    std::ofstream(case_out / (prefix + "_tool_" + std::to_string(step) + ".md")) << chunk.str();
  }
  report.error = "worker sin informe";
  return report;
}

int run_worker_probe(const std::string& root, int argc, char** argv) {
  std::string cards_root_arg;
  std::string out_arg;
  std::string only_case;
  std::string kind;
  std::string stem;
  std::string zone;
  std::string question;
  std::string model_id;
  std::string instruction_arg;
  std::string workspace_arg;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--cards-root" && i + 1 < argc) {
      cards_root_arg = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      out_arg = argv[++i];
    } else if (a == "--case" && i + 1 < argc) {
      only_case = argv[++i];
    } else if (a == "--kind" && i + 1 < argc) {
      kind = argv[++i];
    } else if (a == "--stem" && i + 1 < argc) {
      stem = argv[++i];
    } else if (a == "--zone" && i + 1 < argc) {
      zone = argv[++i];
    } else if (a == "--question" && i + 1 < argc) {
      question = argv[++i];
    } else if (a == "--model-id" && i + 1 < argc) {
      model_id = argv[++i];
    } else if (a == "--instruction" && i + 1 < argc) {
      instruction_arg = argv[++i];
    } else if (a == "--workspace" && i + 1 < argc) {
      workspace_arg = argv[++i];
    } else if (a == "-h" || a == "--help") {
      std::cerr << "worker-probe --kind cubre|como|gap --case ID --stem S --out DIR\n"
                   "  [--cards-root DIR] [--zone M*] [--question …] [--model-id ID]\n"
                   "  [--instruction TXT] [--workspace DIR]\n"
                   "  Un worker sordo sobre una ficha inspect/atlas ya guardada. Sin piloto.\n";
      return 2;
    }
  }
  if (only_case.empty() || stem.empty() || out_arg.empty() || kind.empty()) {
    std::cerr << "worker-probe: requiere --kind, --case, --stem y --out\n";
    return 2;
  }
  if (kind != "cubre" && kind != "como" && kind != "gap") {
    std::cerr << "worker-probe: kind debe ser cubre|como|gap\n";
    return 2;
  }
  fs::path cards_root(cards_root_arg.empty()
                          ? (fs::path(root) / ".tuide/ai/l2_explore_battery/round_atlas20_pilot_workers2")
                          : fs::path(cards_root_arg));
  if (!cards_root.is_absolute()) {
    cards_root = fs::path(root) / cards_root;
  }
  fs::path output_root(out_arg);
  if (!output_root.is_absolute()) {
    output_root = fs::path(root) / output_root;
  }
  const fs::path prompts_path =
      fs::path(root) / "tests/fixtures/stem_boost_battery/prompts_nl_human.json";
  std::string instruction = instruction_arg;
  if (instruction.empty()) {
    std::ifstream prompts_in(prompts_path);
    if (!prompts_in) {
      std::cerr << "worker-probe: no se pudo leer " << prompts_path << "\n";
      return 2;
    }
    nlohmann::json cases;
    prompts_in >> cases;
    for (const auto& item : cases) {
      if (item.value("id", "") == only_case) {
        instruction = item.value("prompt", "");
        break;
      }
    }
  }
  if (instruction.empty()) {
    std::cerr << "worker-probe: caso desconocido " << only_case << "\n";
    return 2;
  }
  fs::path case_dir = cards_root / only_case;
  if (!fs::exists(case_dir / "inspect.json") && !fs::exists(case_dir / "judge_cards.json") &&
      fs::exists(cards_root / "survey" / only_case / "inspect.json")) {
    case_dir = cards_root / "survey" / only_case;
  }
  fs::path inspect_path = case_dir / "inspect.json";
  fs::path cards_path = case_dir / "judge_cards.json";
  nlohmann::json payload;
  tuide::GraphViewLevel view = tuide::GraphViewLevel::Inspect;
  try {
    if (fs::exists(inspect_path)) {
      payload = nlohmann::json::parse(read_file(inspect_path));
    } else if (fs::exists(cards_path)) {
      payload = nlohmann::json::parse(read_file(cards_path));
      view = tuide::GraphViewLevel::Atlas;
    } else {
      std::cerr << "worker-probe: sin inspect.json ni judge_cards.json en " << case_dir << "\n";
      return 1;
    }
  } catch (...) {
    std::cerr << "worker-probe: JSON de ficha inválido\n";
    return 1;
  }
  if (zone.empty()) {
    zone = tuide::registry_causal_zone_id_for_stem(payload, stem);
  }
  if (zone.empty()) {
    std::cerr << "worker-probe: no hay zona para stem=" << stem << "\n";
    return 1;
  }
  if (question.empty()) {
    if (kind == "cubre") {
      question = "¿es " + stem + " el objeto de la consulta?";
    } else if (kind == "como") {
      question = "¿quién escribe, limpia o dispara la acción en " + stem + "?";
    } else {
      question = "¿qué eslabón falta en " + stem + " para explicar la consulta?";
    }
  }
  AiSettings settings = load_ai_settings(root);
  if (!model_id.empty()) {
    settings.level2.model_id = model_id;
    settings.level2.model_path.clear();
  }
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  auto brain_ptr = ready_l2_brain(settings, progress, &err);
  if (!brain_ptr) {
    std::cerr << "worker-probe: " << err << "\n";
    return 1;
  }
  fs::path workspace = workspace_arg.empty() ? fs::path(root) : fs::path(workspace_arg);
  if (!workspace.is_absolute()) {
    workspace = fs::path(root) / workspace;
  }
  const fs::path case_out = output_root;
  fs::create_directories(case_out);
  std::ofstream(case_out / "inspect.json") << payload.dump(2) << "\n";
  tuide::RegistryCausalPilotTask task;
  task.kind = kind;
  task.zone = zone;
  task.stem = stem;
  task.question = question;
  const auto wr = run_one_pilot_worker(*brain_ptr, settings, workspace.string(), case_out, 1, task,
                                       payload, payload, view, instruction);
  const auto wj = tuide::registry_causal_pilot_worker_to_json(wr);
  nlohmann::json summary = wj;
  summary["id"] = only_case;
  summary["kind"] = kind;
  summary["zone"] = zone;
  summary["stem"] = stem;
  summary["question"] = question;
  std::ofstream(case_out / "w1.json") << wj.dump(2) << "\n";
  std::ofstream(case_out / "w1.md") << tuide::registry_causal_pilot_worker_markdown(wr);
  std::ofstream(case_out / "summary.json") << summary.dump(2) << "\n";
  std::cout << summary.dump(2) << "\n";
  return wr.ok && !wr.is_tool ? 0 : 1;
}

int run_atlas_survey(const std::string& root, int argc, char** argv) {
  std::string cards_root_arg;
  std::string out_arg;
  std::string only_case;
  std::string model_id;
  bool pilot_plan = false;
  bool pilot_workers = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--cards-root" && i + 1 < argc) {
      cards_root_arg = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      out_arg = argv[++i];
    } else if (a == "--only" && i + 1 < argc) {
      only_case = argv[++i];
    } else if (a == "--model-id" && i + 1 < argc) {
      model_id = argv[++i];
    } else if (a == "--pilot-plan") {
      pilot_plan = true;
    } else if (a == "--pilot-workers") {
      pilot_plan = true;
      pilot_workers = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "atlas-survey --cards-root DIR --out DIR --only ID [--pilot-plan|--pilot-workers]\n"
                   "  Pasada 0: atlas. LLM elige 1–3 ids por cobertura (no diversidad).\n"
                   "  Pasada 1: inspect + review de cobertura (añade hasta 2 M*).\n"
                   "  Pasada 2: hipótesis; masa baja → una ampliación need_more y se reintenta.\n"
                   "  --pilot-plan: survey + pack compacto; el piloto planifica o pide 1 ampliación.\n"
                   "  --pilot-workers: plan + trabajadores (need_code|outline|follow) + plenario.\n";
      return 2;
    }
  }
  if (cards_root_arg.empty() || out_arg.empty() || only_case.empty()) {
    std::cerr << "atlas-survey: requiere --cards-root, --out y --only\n";
    return 2;
  }
  fs::path cards_root(cards_root_arg);
  fs::path output_root(out_arg);
  if (!cards_root.is_absolute()) {
    cards_root = fs::path(root) / cards_root;
  }
  if (!output_root.is_absolute()) {
    output_root = fs::path(root) / output_root;
  }
  const fs::path prompts_path =
      fs::path(root) / "tests/fixtures/stem_boost_battery/prompts_nl_human.json";
  std::ifstream prompts_in(prompts_path);
  if (!prompts_in) {
    std::cerr << "atlas-survey: no se pudo leer " << prompts_path << "\n";
    return 2;
  }
  nlohmann::json cases;
  prompts_in >> cases;
  std::string instruction;
  for (const auto& item : cases) {
    if (item.value("id", "") == only_case) {
      instruction = item.value("prompt", "");
      break;
    }
  }
  if (instruction.empty()) {
    std::cerr << "atlas-survey: caso desconocido " << only_case << "\n";
    return 2;
  }
  const fs::path payload_path = cards_root / only_case / "judge_cards.json";
  nlohmann::json base_payload;
  try {
    base_payload = nlohmann::json::parse(read_file(payload_path));
  } catch (...) {
    std::cerr << "atlas-survey: no se pudo leer " << payload_path << "\n";
    return 1;
  }
  AiSettings settings = load_ai_settings(root);
  if (!model_id.empty()) {
    settings.level2.model_id = model_id;
    settings.level2.model_path.clear();
  }
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  auto brain_ptr = ready_l2_brain(settings, progress, &err);
  if (!brain_ptr) {
    std::cerr << "atlas-survey: " << err << "\n";
    return 1;
  }
  tuide::L2Brain& brain = *brain_ptr;
  tuide::EffectRegistry expansion_registry;
  if (!tuide::registry_open(root, &expansion_registry, &err)) {
    std::cerr << "atlas-survey: registry: " << err << "\n";
    return 1;
  }
  const fs::path case_out = output_root / only_case;
  fs::create_directories(case_out);
  const std::string atlas_md = tuide::registry_causal_atlas_markdown(base_payload, instruction);
  const auto zone_ids = zone_ids_from_payload(base_payload);
  const auto allowed_targets = triage_targets_by_zone(base_payload);
  std::ofstream(case_out / "atlas.md") << atlas_md;
  tuide::L2BrainRequest req;
  req.system_prompt = tuide::registry_causal_atlas_survey_system_prompt();
  req.user_prompt = "## Consulta\n" + instruction + "\n\n" +
                    tuide::registry_causal_atlas_survey_user_prompt(atlas_md);
  req.phase = "causal_atlas_survey";
  // CoT-in-content (hub sin --reasoning-format deepseek) necesita holgura
  // para el razonamiento Low y el JSON. Con deepseek, EOS corta tras el JSON.
  req.max_tokens =
      std::min(2048, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 2048);
  req.n_ctx = std::max(4096, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 4096);
  req.temperature = 0.05f;
  std::ofstream(case_out / "survey_system.txt") << req.system_prompt;
  std::ofstream(case_out / "survey_user.md") << req.user_prompt;
  const auto t0 = std::chrono::steady_clock::now();
  const auto response = tuide::propose_with_think(brain, &req, nullptr);
  const int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
  std::ofstream(case_out / "survey_raw.txt") << response.text;
  tuide::RegistryCausalTriageDecision survey;
  if (!response.ok) {
    survey.error = response.error;
    survey.raw = response.text;
  } else {
    survey = tuide::registry_parse_causal_atlas_survey(response.text, zone_ids, allowed_targets);
  }
  std::ofstream(case_out / "survey.json")
      << tuide::registry_causal_triage_decision_to_json(survey).dump(2) << "\n";
  nlohmann::json summary = {{"id", only_case},
                            {"ok", survey.ok},
                            {"elapsed_ms", elapsed_ms},
                            {"atlas_chars", atlas_md.size()},
                            {"shortlist", survey.shortlist},
                            {"view", survey.view},
                            {"why", survey.why},
                            {"error", survey.error}};
  if (!survey.ok && survey.shortlist.empty()) {
    std::ofstream(case_out / "summary.json") << summary.dump(2) << "\n";
    std::cout << summary.dump(2) << "\n";
    tuide::registry_close(&expansion_registry);
    return 1;
  }
  tuide::GraphViewLevel next_view = tuide::GraphViewLevel::Inspect;
  tuide::graph_view_level_parse(survey.view, &next_view);
  tuide::RegistryCausalJudgeOpts expanded_opts;
  tuide::graph_view_profile_apply(tuide::graph_view_profile_default(next_view), &expanded_opts);

  auto expand_survey = [&](tuide::RegistryCausalTriageDecision& surv, nlohmann::json* out_payload,
                           std::string* expand_err) -> bool {
    if (surv.shortlist.empty()) {
      *expand_err = "shortlist vacío";
      return false;
    }
    tuide::registry_atlas_fill_expand_from(base_payload, &surv);
    if (tuide::registry_expand_causal_judge_payload(&expansion_registry, base_payload, surv,
                                                    expanded_opts, out_payload, expand_err)) {
      return true;
    }
    for (auto& zone : surv.zones) {
      zone.expand_from.clear();
    }
    if (tuide::registry_expand_causal_judge_payload(&expansion_registry, base_payload, surv,
                                                    expanded_opts, out_payload, expand_err)) {
      summary["expand_fallback"] = "empty_expand_from";
      return true;
    }
    return false;
  };

  nlohmann::json expanded_payload;
  std::string inspect_md;
  if (!survey.shortlist.empty()) {
    if (!expand_survey(survey, &expanded_payload, &err)) {
      summary["expand_error"] = err;
      std::ofstream(case_out / "summary.json") << summary.dump(2) << "\n";
      std::cout << summary.dump(2) << "\n";
      tuide::registry_close(&expansion_registry);
      return 1;
    }
    inspect_md = tuide::registry_causal_pack_markdown(expanded_payload, next_view);
    std::ofstream(case_out / "inspect_r1.md") << inspect_md;
  }
  summary["inspect_r1_zones"] = survey.shortlist;

  auto json_ids = [](const nlohmann::json& arr) {
    std::vector<std::string> out;
    if (!arr.is_array()) {
      return out;
    }
    for (const auto& item : arr) {
      if (item.is_string()) {
        out.push_back(item.get<std::string>());
      }
    }
    return out;
  };
  constexpr int kAtlasInspectCap = 4;
  const auto ov = tuide::registry_atlas_overlap_add_ids(base_payload, instruction,
                                                       survey.shortlist);
  const auto ov_ids = json_ids(ov);
  summary["overlap_survey_add"] = ov;
  if (!pilot_plan) {
    const int nadd =
        tuide::registry_atlas_merge_inspect_ids(&survey, ov_ids, zone_ids, kAtlasInspectCap);
    summary["overlap_survey_merged"] = nadd;
    if (nadd > 0) {
      if (!expand_survey(survey, &expanded_payload, &err)) {
        summary["overlap_survey_expand_error"] = err;
      } else {
        inspect_md = tuide::registry_causal_pack_markdown(expanded_payload, next_view);
      }
    }
  } else {
    summary["overlap_suggest"] = ov;
    summary["overlap_survey_merged"] = 0;
  }

  std::vector<std::string> remaining_ids;
  for (const auto& zid : zone_ids) {
    if (std::find(survey.shortlist.begin(), survey.shortlist.end(), zid) ==
        survey.shortlist.end()) {
      remaining_ids.push_back(zid);
    }
  }
  if (!pilot_plan) {
  tuide::L2BrainRequest cover_req;
  cover_req.system_prompt = tuide::registry_causal_atlas_cover_system_prompt();
  cover_req.user_prompt =
      "## Consulta\n" + instruction + "\n\n" +
      tuide::registry_causal_atlas_cover_user_prompt(atlas_md, survey.shortlist, remaining_ids,
                                                    inspect_md);
  cover_req.phase = "causal_atlas_cover";
  cover_req.max_tokens =
      std::min(256, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 256);
  cover_req.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
  cover_req.temperature = 0.05f;
  std::ofstream(case_out / "cover_system.txt") << cover_req.system_prompt;
  std::ofstream(case_out / "cover_user.md") << cover_req.user_prompt;
  const auto cover_t0 = std::chrono::steady_clock::now();
  const auto cover_response = tuide::propose_with_think(brain, &cover_req, nullptr);
  const int64_t cover_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - cover_t0)
                               .count();
  std::ofstream(case_out / "cover_raw.txt") << cover_response.text;
  tuide::RegistryCausalAtlasCoverDecision cover;
  if (!cover_response.ok) {
    cover.error = cover_response.error;
    cover.raw = cover_response.text;
  } else {
    cover = tuide::registry_parse_causal_atlas_cover(cover_response.text, zone_ids,
                                                     survey.shortlist);
  }
  summary["cover_ok"] = cover.ok;
  summary["cover_covers"] = cover.covers;
  summary["cover_add"] = cover.add;
  summary["cover_why"] = cover.why;
  summary["cover_error"] = cover.error;
  summary["cover_ms"] = cover_ms;
  std::ofstream(case_out / "cover.json")
      << nlohmann::json({{"ok", cover.ok},
                         {"covers", cover.covers},
                         {"add", cover.add},
                         {"why", cover.why},
                         {"error", cover.error}})
             .dump(2)
         << "\n";
  if (cover.ok && !cover.add.empty()) {
    const int nadd =
        tuide::registry_atlas_merge_inspect_ids(&survey, cover.add, zone_ids, kAtlasInspectCap);
    summary["cover_merged"] = nadd;
    if (nadd > 0) {
      if (!expand_survey(survey, &expanded_payload, &err)) {
        summary["cover_expand_error"] = err;
      } else {
        inspect_md = tuide::registry_causal_pack_markdown(expanded_payload, next_view);
      }
    }
  }
  {
    const auto ov = tuide::registry_atlas_overlap_add_ids(base_payload, instruction,
                                                         survey.shortlist);
    const auto ov_ids = json_ids(ov);
    const int nadd =
        tuide::registry_atlas_merge_inspect_ids(&survey, ov_ids, zone_ids, kAtlasInspectCap);
    summary["overlap_cover_add"] = ov;
    summary["overlap_cover_merged"] = nadd;
    if (nadd > 0) {
      cover.covers = false;
      if (!expand_survey(survey, &expanded_payload, &err)) {
        summary["overlap_cover_expand_error"] = err;
      } else {
        inspect_md = tuide::registry_causal_pack_markdown(expanded_payload, next_view);
      }
    }
  }
  }
  summary["ok"] = survey.ok && !survey.shortlist.empty();
  summary["shortlist"] = survey.shortlist;
  std::ofstream(case_out / "survey.json")
      << tuide::registry_causal_triage_decision_to_json(survey).dump(2) << "\n";
  if (survey.shortlist.empty()) {
    summary["error"] = "sin zonas tras cover";
    std::ofstream(case_out / "summary.json") << summary.dump(2) << "\n";
    std::cout << summary.dump(2) << "\n";
    tuide::registry_close(&expansion_registry);
    return 1;
  }
  std::ofstream(case_out / "inspect.md") << inspect_md;
  std::ofstream(case_out / "inspect.json") << expanded_payload.dump(2) << "\n";
  summary["inspect_chars"] = inspect_md.size();
  summary["inspect_zones"] = zone_ids_from_payload(expanded_payload);
  summary["needs"] = nlohmann::json::array();
  for (const auto& zone : survey.zones) {
    summary["needs"].push_back({{"id", zone.id}, {"need", zone.need},
                                {"expand_from", zone.expand_from}});
  }

  auto remaining_now = [&]() {
    std::vector<std::string> rem;
    for (const auto& zid : zone_ids) {
      if (std::find(survey.shortlist.begin(), survey.shortlist.end(), zid) ==
          survey.shortlist.end()) {
        rem.push_back(zid);
      }
    }
    return rem;
  };

  if (pilot_plan) {
    auto pack_src = [&]() -> const nlohmann::json& {
      if (expanded_payload.contains("zones") && !expanded_payload["zones"].empty()) {
        return expanded_payload;
      }
      return base_payload;
    };
    tuide::RegistryCausalPilotPlan plan;
    int64_t plan_ms = 0;
    std::string last_plan_raw;
    for (int pass = 0; pass < 2; ++pass) {
      const auto rem = remaining_now();
      nlohmann::json pack_payload = pack_src();
      const std::string opened_pack = tuide::registry_causal_pilot_opened_pack(
          pack_payload, survey.shortlist, instruction);
      const std::vector<std::string> suggest = pass == 0 ? ov_ids : std::vector<std::string>{};
      tuide::L2BrainRequest plan_req;
      const bool allow_more = pass == 0;
      plan_req.system_prompt = tuide::registry_causal_pilot_plan_system_prompt(allow_more);
      plan_req.user_prompt = "## Consulta\n" + instruction + "\n\n" +
                             tuide::registry_causal_pilot_plan_user_prompt(
                                 atlas_md, opened_pack, rem, suggest, allow_more);
      plan_req.phase = pass == 0 ? "causal_pilot_plan" : "causal_pilot_plan_more";
      plan_req.max_tokens =
          std::min(1024, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 1024);
      plan_req.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
      plan_req.temperature = 0.05f;
      const std::string pass_tag = pass == 0 ? "" : "_more";
      std::ofstream(case_out / ("plan_system" + pass_tag + ".txt")) << plan_req.system_prompt;
      std::ofstream(case_out / ("plan_user" + pass_tag + ".md")) << plan_req.user_prompt;
      const auto plan_t0 = std::chrono::steady_clock::now();
      const auto plan_response = tuide::propose_with_think(brain, &plan_req, nullptr);
      last_plan_raw = plan_response.text;
      plan_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - plan_t0)
                     .count();
      std::ofstream(case_out / ("plan_raw" + pass_tag + ".txt")) << plan_response.text;
      if (!plan_response.ok) {
        plan = tuide::RegistryCausalPilotPlan{};
        plan.error = plan_response.error;
        plan.raw = plan_response.text;
        break;
      }
      plan = tuide::registry_parse_causal_pilot_plan(plan_response.text, base_payload, zone_ids,
                                                    survey.shortlist, instruction, pass == 0);
      if (plan.need_more && plan.ok && pass == 0) {
        const int nadd = tuide::registry_atlas_merge_inspect_ids(&survey, plan.add, zone_ids,
                                                                kAtlasInspectCap);
        summary["pilot_need_more"] = true;
        summary["pilot_need_more_add"] = plan.add;
        summary["pilot_need_more_merged"] = nadd;
        std::ofstream(case_out / "plan_need_more.json")
            << tuide::registry_causal_pilot_plan_to_json(plan).dump(2) << "\n";
        if (nadd > 0) {
          if (!expand_survey(survey, &expanded_payload, &err)) {
            summary["pilot_need_more_expand_error"] = err;
            break;
          }
          inspect_md = tuide::registry_causal_pack_markdown(expanded_payload, next_view);
          std::ofstream(case_out / "inspect.md") << inspect_md;
          std::ofstream(case_out / "inspect.json") << expanded_payload.dump(2) << "\n";
          summary["inspect_chars"] = inspect_md.size();
          summary["inspect_zones"] = zone_ids_from_payload(expanded_payload);
          summary["shortlist"] = survey.shortlist;
        }
        continue;
      }
      break;
    }
    const auto plan_json = tuide::registry_causal_pilot_plan_to_json(plan);
    std::ofstream(case_out / "plan.json") << plan_json.dump(2) << "\n";
    std::ofstream(case_out / "plan.md") << tuide::registry_causal_pilot_plan_markdown(plan);
    summary["pilot_plan"] = true;
    summary["plan_ok"] = plan.ok && !plan.need_more;
    summary["plan_ms"] = plan_ms;
    summary["plan_error"] = plan.error;
    summary["plan_why"] = plan.why;
    summary["plan_tasks"] = plan_json["tasks"];
    summary["plan_unique_stems"] = plan.unique_stems;
    summary["plan_n_cubre"] = plan.n_cubre;
    summary["plan_has_como"] = plan.has_como;
    summary["plan_has_gap"] = plan.has_gap;
    summary["ok"] = survey.ok && plan.ok && !plan.need_more;

    if (pilot_workers && plan.ok && !plan.need_more) {
      std::ostringstream reports_md;
      nlohmann::json reports_json = nlohmann::json::array();
      std::vector<std::string> allowed_stems;
      std::vector<tuide::RegistryCausalPilotWorkerReport> worker_reports;
      int workers_ok = 0;
      int64_t workers_ms = 0;
      for (int i = 0; i < static_cast<int>(plan.tasks.size()); ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        auto wr = run_one_pilot_worker(brain, settings, root, case_out, i + 1, plan.tasks[i],
                                       expanded_payload, base_payload, next_view, instruction);
        workers_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
        if (wr.ok && !wr.is_tool) {
          ++workers_ok;
        }
        allowed_stems.push_back(wr.stem);
        const auto wj = tuide::registry_causal_pilot_worker_to_json(wr);
        reports_json.push_back(wj);
        reports_md << tuide::registry_causal_pilot_worker_markdown(wr) << "\n";
        std::ofstream(case_out / ("w" + std::to_string(i + 1) + ".json"))
            << wj.dump(2) << "\n";
        std::ofstream(case_out / ("w" + std::to_string(i + 1) + ".md"))
            << tuide::registry_causal_pilot_worker_markdown(wr);
        worker_reports.push_back(std::move(wr));
      }
      {
        auto lower = [](std::string s) {
          for (char& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          }
          return s;
        };
        std::unordered_set<std::string> tasked;
        for (const auto& t : plan.tasks) {
          tasked.insert(lower(t.stem));
        }
        std::string extra_stem;
        std::string extra_zone;
        for (const auto& wr : worker_reports) {
          if (wr.port_to.empty()) {
            continue;
          }
          const std::string zid =
              tuide::registry_causal_zone_id_for_stem(base_payload, wr.port_to);
          if (zid.empty() || tasked.count(lower(wr.port_to)) != 0) {
            continue;
          }
          extra_stem = wr.port_to;
          extra_zone = zid;
          break;
        }
        if (!extra_stem.empty()) {
          if (std::find(survey.shortlist.begin(), survey.shortlist.end(), extra_zone) ==
              survey.shortlist.end()) {
            const int nadd = tuide::registry_atlas_merge_inspect_ids(
                &survey, {extra_zone}, zone_ids, kAtlasInspectCap);
            if (nadd > 0) {
              if (expand_survey(survey, &expanded_payload, &err)) {
                inspect_md = tuide::registry_causal_pack_markdown(expanded_payload, next_view);
              }
            }
          }
          tuide::RegistryCausalPilotTask extra;
          extra.kind = "cubre";
          extra.zone = extra_zone;
          extra.stem = extra_stem;
          extra.question = "¿es " + extra_stem + " el objeto de la consulta?";
          const int extra_i = static_cast<int>(plan.tasks.size()) + 1;
          const auto t0 = std::chrono::steady_clock::now();
          auto wr = run_one_pilot_worker(brain, settings, root, case_out, extra_i, extra,
                                         expanded_payload, base_payload, next_view, instruction);
          workers_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
          if (wr.ok && !wr.is_tool) {
            ++workers_ok;
          }
          allowed_stems.push_back(wr.stem);
          const auto wj = tuide::registry_causal_pilot_worker_to_json(wr);
          reports_json.push_back(wj);
          reports_md << tuide::registry_causal_pilot_worker_markdown(wr) << "\n";
          std::ofstream(case_out / ("w" + std::to_string(extra_i) + ".json")) << wj.dump(2) << "\n";
          std::ofstream(case_out / ("w" + std::to_string(extra_i) + ".md"))
              << tuide::registry_causal_pilot_worker_markdown(wr);
          worker_reports.push_back(std::move(wr));
          summary["port_to_extra"] = extra_stem;
          summary["port_to_extra_zone"] = extra_zone;
        }
      }
      std::ofstream(case_out / "workers.md") << reports_md.str();
      summary["pilot_workers"] = true;
      summary["workers_ok"] = workers_ok;
      summary["workers_n"] = static_cast<int>(plan.tasks.size());
      summary["workers_ms"] = workers_ms;
      summary["worker_reports"] = reports_json;

      tuide::L2BrainRequest plen_req;
      plen_req.system_prompt = tuide::registry_causal_pilot_plenary_system_prompt();
      plen_req.user_prompt = "## Consulta\n" + instruction + "\n\n" +
                             tuide::registry_causal_pilot_plenary_user_prompt(reports_md.str());
      plen_req.phase = "causal_pilot_plenary";
      plen_req.max_tokens =
          std::min(384, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 384);
      plen_req.n_ctx = std::max(4096, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 4096);
      plen_req.temperature = 0.05f;
      std::ofstream(case_out / "plenary_system.txt") << plen_req.system_prompt;
      std::ofstream(case_out / "plenary_user.md") << plen_req.user_prompt;
      const auto plen_t0 = std::chrono::steady_clock::now();
      const auto plen_response = tuide::propose_with_think(brain, &plen_req, nullptr);
      const int64_t plen_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - plen_t0)
                                  .count();
      std::ofstream(case_out / "plenary_raw.txt") << plen_response.text;
      tuide::RegistryCausalPilotPlenary plenary;
      if (!plen_response.ok) {
        plenary.error = plen_response.error;
        plenary.raw = plen_response.text;
      } else {
        plenary = tuide::registry_parse_causal_pilot_plenary(plen_response.text, allowed_stems);
      }
      tuide::registry_tally_pilot_plenary(&plenary, worker_reports);
      const auto plen_json = tuide::registry_causal_pilot_plenary_to_json(plenary);
      std::ofstream(case_out / "plenary.json") << plen_json.dump(2) << "\n";
      std::ofstream(case_out / "plenary.md") << tuide::registry_causal_pilot_plenary_markdown(plenary);
      summary["plenary_ok"] = plenary.ok;
      summary["plenary_verdict"] = plenary.verdict;
      summary["plenary_keep"] = plenary.keep;
      summary["plenary_drop"] = plenary.drop;
      summary["plenary_why"] = plenary.why;
      summary["plenary_error"] = plenary.error;
      summary["plenary_ms"] = plen_ms;
      summary["ok"] = survey.ok && plan.ok && plenary.ok;
    }

    std::ofstream(case_out / "summary.json") << summary.dump(2) << "\n";
    std::cout << summary.dump(2) << "\n";
    std::cout << "\n===== atlas =====\n" << atlas_md << "\n===== plan =====\n"
              << tuide::registry_causal_pilot_plan_markdown(plan) << "\n===== plan raw =====\n"
              << last_plan_raw << "\n";
    tuide::registry_close(&expansion_registry);
    if (pilot_workers) {
      return summary.value("plenary_ok", false) ? 0 : 1;
    }
    return plan.ok ? 0 : 1;
  }

  tuide::RegistryCausalHypDecision hyp;
  std::string last_hyp_raw;
  int64_t hyp_ms = 0;
  bool did_more = false;
  for (int pass = 0; pass < 2; ++pass) {
    const auto rem = remaining_now();
    tuide::L2BrainRequest hyp_req;
    hyp_req.system_prompt = tuide::registry_causal_zone_hyp_system_prompt();
    hyp_req.user_prompt = "## Consulta\n" + instruction + "\n\n" +
                          tuide::registry_causal_zone_hyp_user_prompt(inspect_md, rem);
    hyp_req.phase = pass == 0 ? "causal_zone_hyp" : "causal_zone_hyp_more";
    hyp_req.max_tokens =
        std::min(512, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 512);
    hyp_req.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
    hyp_req.temperature = 0.05f;
    const std::string sys_name = pass == 0 ? "hyp_system.txt" : "hyp_more_system.txt";
    const std::string user_name = pass == 0 ? "hyp_user.md" : "hyp_more_user.md";
    std::ofstream(case_out / sys_name) << hyp_req.system_prompt;
    std::ofstream(case_out / user_name) << hyp_req.user_prompt;
    const auto hyp_t0 = std::chrono::steady_clock::now();
    const auto hyp_response = tuide::propose_with_think(brain, &hyp_req, nullptr);
    hyp_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - hyp_t0)
                  .count();
    last_hyp_raw = hyp_response.text;
    const std::string raw_name = pass == 0 ? "hyp_raw.txt" : "hyp_more_raw.txt";
    std::ofstream(case_out / raw_name) << hyp_response.text;
    if (!hyp_response.ok) {
      hyp = tuide::RegistryCausalHypDecision{};
      hyp.error = hyp_response.error;
      hyp.raw = hyp_response.text;
    } else {
      hyp = tuide::registry_parse_causal_zone_hyp(hyp_response.text, expanded_payload, zone_ids);
    }
    if (hyp.ok || pass == 1) {
      break;
    }
    std::vector<std::string> add = hyp.add;
    if (add.empty()) {
      add = tuide::registry_atlas_suggest_cover_ids(base_payload, survey.shortlist, 2);
    }
    bool added = false;
    if (!add.empty()) {
      const int nadd =
          tuide::registry_atlas_merge_inspect_ids(&survey, add, zone_ids, kAtlasInspectCap);
      summary["hyp_more_merged"] = nadd;
      added = nadd > 0;
    }
    if (!hyp.expand_from.empty()) {
      for (auto& zone : survey.zones) {
        if (zone.expand_from.empty()) {
          zone.expand_from = hyp.expand_from;
        }
      }
    }
    tuide::GraphViewLevel more_view = next_view;
    if (!added) {
      more_view = tuide::GraphViewLevel::Deep;
      tuide::graph_view_profile_apply(tuide::graph_view_profile_default(more_view),
                                      &expanded_opts);
    }
    if (!expand_survey(survey, &expanded_payload, &err)) {
      summary["hyp_more_expand_error"] = err;
      break;
    }
    inspect_md = tuide::registry_causal_pack_markdown(expanded_payload, more_view);
    std::ofstream(case_out / "inspect.md") << inspect_md;
    std::ofstream(case_out / "inspect.json") << expanded_payload.dump(2) << "\n";
    summary["inspect_chars"] = inspect_md.size();
    summary["inspect_zones"] = zone_ids_from_payload(expanded_payload);
    summary["shortlist"] = survey.shortlist;
    summary["hyp_more"] = true;
    summary["hyp_more_add"] = add;
    summary["hyp_more_view"] = tuide::graph_view_level_name(more_view);
    did_more = true;
  }
  std::ofstream(case_out / "hyp.json")
      << tuide::registry_causal_hyp_decision_to_json(hyp).dump(2) << "\n";
  summary["hyp_ok"] = hyp.ok;
  summary["hyp_parsed"] = hyp.parsed;
  summary["hyp_need_more"] = hyp.need_more;
  summary["hyp_mass"] = hyp.mass;
  summary["hyp_band"] = hyp.mass_band;
  summary["hyp_ms"] = hyp_ms;
  summary["hyp_error"] = hyp.error;
  summary["hyp_why"] = hyp.why;
  summary["hyp_more"] = did_more;
  summary["hypotheses"] = tuide::registry_causal_hyp_decision_to_json(hyp)["hypotheses"];
  if (hyp.ok) {
    tuide::ProblemFrame pf;
    pf.schema = tuide::kProblemFrameSchema;
    pf.instruction = instruction;
    pf.problem_kind = "debug";
    pf.problem_frame = instruction;
    pf.provenance = "causal_atlas_hyp";
    pf.anchor_confidence = hyp.mass_band == "high" ? "high" : "medium";
    pf.anchor_hypotheses = hyp.hypotheses;
    if (!hyp.hypotheses.empty()) {
      const auto& h0 = hyp.hypotheses.front();
      pf.primary_anchor.kind = "state";
      pf.primary_anchor.objective = h0.claim;
      pf.primary_anchor.search_terms = h0.search_terms;
      pf.active_hypothesis_index = 0;
    }
    std::ofstream(case_out / "problem_frame.json")
        << tuide::problem_frame_to_json(pf).dump(2) << "\n";
  }

  std::ofstream(case_out / "summary.json") << summary.dump(2) << "\n";
  std::cout << summary.dump(2) << "\n";
  std::cout << "\n===== atlas =====\n" << atlas_md << "\n===== survey raw =====\n"
            << response.text << "\n===== hyp raw =====\n" << last_hyp_raw
            << "\n===== hyp =====\n"
            << tuide::registry_causal_hyp_decision_to_json(hyp).dump(2) << "\n";
  tuide::registry_close(&expansion_registry);
  return hyp.ok ? 0 : 1;
}

int run_zone_judge_battery(const std::string& root, int argc, char** argv) {
  std::string cards_root_arg;
  std::string out_arg;
  std::string start_at;
  std::string only_case;
  std::string model_id;
  bool two_pass = false;
  bool legacy_triage = false;
  bool primary_survey = false;
  bool slot_survey = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--cards-root" && i + 1 < argc) {
      cards_root_arg = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      out_arg = argv[++i];
    } else if (a == "--start-at" && i + 1 < argc) {
      start_at = argv[++i];
    } else if (a == "--only" && i + 1 < argc) {
      only_case = argv[++i];
    } else if (a == "--model-id" && i + 1 < argc) {
      model_id = argv[++i];
    } else if (a == "--two-pass") {
      two_pass = true;
    } else if (a == "--legacy-triage") {
      legacy_triage = true;
    } else if (a == "--primary-survey") {
      primary_survey = true;
    } else if (a == "--slot-survey") {
      slot_survey = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "zone-judge-battery --cards-root DIR --out DIR [--start-at ID] [--only ID] "
                   "[--two-pass] [--legacy-triage] [--primary-survey] [--slot-survey]\n"
                   "  Un único L2Brain; --two-pass hace ancla→expansión→síntesis "
                   "(opcional 1 reapertura). --legacy-triage usa triage inspect viejo.\n"
                   "  --primary-survey (con --two-pass): contraste must-compete → top-2 hilos.\n"
                   "  --slot-survey (con --two-pass): hyp por slot (1 ficha/pass) → pool 2–3.\n";
      return 2;
    }
  }
  if (cards_root_arg.empty() || out_arg.empty()) {
    std::cerr << "zone-judge-battery: requiere --cards-root y --out\n";
    return 2;
  }
  fs::path cards_root(cards_root_arg);
  fs::path output_root(out_arg);
  if (!cards_root.is_absolute()) {
    cards_root = fs::path(root) / cards_root;
  }
  if (!output_root.is_absolute()) {
    output_root = fs::path(root) / output_root;
  }
  fs::create_directories(output_root);
  const fs::path prompts_path =
      fs::path(root) / "tests/fixtures/stem_boost_battery/prompts_nl_human.json";
  std::ifstream prompts_in(prompts_path);
  nlohmann::json cases;
  if (!prompts_in) {
    std::cerr << "zone-judge-battery: no se pudo leer " << prompts_path << "\n";
    return 2;
  }
  prompts_in >> cases;

  AiSettings settings = load_ai_settings(root);
  if (!model_id.empty()) {
    settings.level2.model_id = model_id;
    settings.level2.model_path.clear();
  }
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  auto brain_ptr = ready_l2_brain(settings, progress, &err);
  if (!brain_ptr) {
    std::cerr << "zone-judge-battery: " << err << "\n";
    return 1;
  }
  tuide::L2Brain& brain = *brain_ptr;
  const bool epistemic = two_pass && !legacy_triage;
  const bool use_slot_survey = epistemic && slot_survey;
  const bool use_primary_survey = epistemic && primary_survey && !use_slot_survey;
  const std::string system = epistemic ? tuide::registry_causal_synth_system_prompt()
                                       : tuide::registry_causal_judge_system_prompt();
  tuide::EffectRegistry expansion_registry;
  if (two_pass && !tuide::registry_open(root, &expansion_registry, &err)) {
    std::cerr << "zone-judge-battery: registry: " << err << "\n";
    return 1;
  }
  bool started = start_at.empty();
  int total = 0;
  int valid = 0;
  int hit = 0;
  int operational_hit = 0;
  int trap = 0;
  nlohmann::json rows = nlohmann::json::array();
  for (const auto& item : cases) {
    const std::string id = item.value("id", "");
    if (!only_case.empty() && id != only_case) {
      continue;
    }
    if (!started) {
      started = id == start_at;
    }
    if (!started || id.empty()) {
      continue;
    }
    ++total;
    const fs::path case_out = output_root / id;
    fs::create_directories(case_out);
    const std::string instruction = item.value("prompt", "");
    std::string cards;
    std::vector<std::string> zone_ids;
    std::string hypothesis;
    std::string anchor_why;
    int64_t triage_ms = 0;
    bool reopened = false;
    if (two_pass) {
      const fs::path payload_path = cards_root / id / "judge_cards.json";
      nlohmann::json base_payload;
      try {
        base_payload = nlohmann::json::parse(read_file(payload_path));
      } catch (...) {
        rows.push_back({{"id", id}, {"ok", false}, {"error", "missing_payload"}});
        continue;
      }
      const std::string triage_cards = tuide::registry_causal_triage_markdown(base_payload);
      const auto triage_ids = zone_ids_from_payload(base_payload);
      const auto allowed_targets = triage_targets_by_zone(base_payload);
      if (triage_ids.empty()) {
        rows.push_back({{"id", id}, {"ok", false}, {"error", "empty_triage"}});
        continue;
      }

      auto run_anchor_or_triage =
          [&](const std::string& reopen_need, const std::string& prefix)
          -> tuide::RegistryCausalTriageDecision {
        tuide::L2BrainRequest triage_request;
        if (epistemic) {
          triage_request.system_prompt = tuide::registry_causal_anchor_system_prompt();
          triage_request.user_prompt =
              "## Consulta\n" + instruction + "\n\n" +
              tuide::registry_causal_anchor_user_prompt(triage_cards, reopen_need);
          triage_request.phase = "causal_zone_anchor";
          triage_request.max_tokens =
              std::min(384, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 384);
        } else {
          triage_request.system_prompt = tuide::registry_causal_triage_system_prompt();
          triage_request.user_prompt =
              "## Consulta\n" + instruction + "\n\n" +
              tuide::registry_causal_triage_user_prompt(triage_cards);
          triage_request.phase = "causal_zone_triage";
          triage_request.max_tokens =
              std::min(192, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 192);
        }
        triage_request.n_ctx =
            std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
        triage_request.temperature = 0.05f;
        const auto triage_t0 = std::chrono::steady_clock::now();
        const auto triage_response = tuide::propose_with_think(brain, &triage_request, nullptr);
        triage_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - triage_t0)
                         .count();
        std::ofstream(case_out / (prefix + "_system.txt")) << triage_request.system_prompt;
        std::ofstream(case_out / (prefix + "_user.md")) << triage_request.user_prompt;
        std::ofstream(case_out / (prefix + "_cards.md")) << triage_cards;
        std::ofstream(case_out / (prefix + "_raw.txt")) << triage_response.text;
        tuide::RegistryCausalTriageDecision triage;
        if (!triage_response.ok) {
          triage.error = triage_response.error;
          return triage;
        }
        if (epistemic) {
          triage = tuide::registry_parse_causal_anchor_decision(
              triage_response.text, triage_ids, allowed_targets);
        } else {
          triage = tuide::registry_parse_causal_triage_decision(
              triage_response.text, triage_ids, allowed_targets);
        }
        std::ofstream(case_out / (prefix + ".json"))
            << tuide::registry_causal_triage_decision_to_json(triage).dump(2) << "\n";
        // Compat scoring: mirror first pass as triage.*
        if (prefix == "anchor" || prefix == "triage") {
          std::ofstream(case_out / "triage_system.txt") << triage_request.system_prompt;
          std::ofstream(case_out / "triage_user.md") << triage_request.user_prompt;
          std::ofstream(case_out / "triage_cards.md") << triage_cards;
          std::ofstream(case_out / "triage_raw.txt") << triage_response.text;
          std::ofstream(case_out / "triage.json")
              << tuide::registry_causal_triage_decision_to_json(triage).dump(2) << "\n";
        }
        return triage;
      };

      auto expand_from_decision =
          [&](const tuide::RegistryCausalTriageDecision& triage, nlohmann::json* expanded_payload)
          -> bool {
        tuide::RegistryCausalJudgeOpts expanded_opts;
        expanded_opts.max_zones = 3;
        expanded_opts.max_representatives = 10;
        expanded_opts.max_edges = 24;
        expanded_opts.max_trails = 2;
        if (epistemic && !triage.critical_mass) {
          expanded_opts.expand_hops = 1;
          expanded_opts.max_representatives = 6;
          expanded_opts.max_edges = 12;
        } else {
          expanded_opts.expand_hops = 2;
        }
        return tuide::registry_expand_causal_judge_payload(
            &expansion_registry, base_payload, triage, expanded_opts, expanded_payload, &err);
      };

      auto run_synth = [&](const std::string& synth_cards,
                           const std::vector<std::string>& synth_zone_ids,
                           const std::string& hyp, const std::string& why_anchor,
                           const std::string& artifact_prefix) -> tuide::RegistryCausalJudgeDecision {
        const std::string user =
            epistemic ? ("## Consulta\n" + instruction + "\n\n" +
                         tuide::registry_causal_synth_user_prompt(synth_cards, hyp, why_anchor))
                      : ("## Consulta\n" + instruction + "\n\n" +
                         tuide::registry_causal_judge_user_prompt(synth_cards));
        tuide::L2BrainRequest request;
        request.system_prompt = system;
        request.user_prompt = user;
        request.phase = epistemic ? "causal_zone_synth" : "causal_zone_judge";
        const int judge_token_cap = 384;
        request.max_tokens = std::min(
            judge_token_cap,
            settings.level2.max_tokens > 0 ? settings.level2.max_tokens : judge_token_cap);
        request.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
        request.temperature = 0.05f;
        const auto t0 = std::chrono::steady_clock::now();
        const auto response = tuide::propose_with_think(brain, &request, nullptr);
        const int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - t0)
                                       .count();
        std::ofstream(case_out / (artifact_prefix + "system.txt")) << system;
        std::ofstream(case_out / (artifact_prefix + "user.md")) << user;
        std::ofstream(case_out / (artifact_prefix + "cards.md")) << synth_cards;
        std::ofstream(case_out / (artifact_prefix + "model_raw.txt")) << response.text;
        tuide::RegistryCausalJudgeDecision synth_decision;
        if (!response.ok) {
          synth_decision.error = response.error;
          synth_decision.raw = response.text;
          std::ofstream(case_out / (artifact_prefix + "decision.json"))
              << tuide::registry_causal_judge_decision_to_json(synth_decision).dump(2) << "\n";
          (void)elapsed_ms;
          return synth_decision;
        }
        synth_decision = tuide::registry_parse_causal_judge_decision(response.text, synth_zone_ids);
        if (synth_decision.ok) {
          for (const auto& verdict : synth_decision.zones) {
            for (const auto& symbol : verdict.expand_from) {
              if (synth_cards.find(symbol) == std::string::npos) {
                synth_decision.ok = false;
                synth_decision.error =
                    "expand_from no pertenece a la ficha " + verdict.id + ": " + symbol;
                break;
              }
            }
            if (!synth_decision.ok) {
              break;
            }
          }
        }
        std::ofstream(case_out / (artifact_prefix + "decision.json"))
            << tuide::registry_causal_judge_decision_to_json(synth_decision).dump(2) << "\n";
        return synth_decision;
      };

      const std::string first_prefix = epistemic ? "anchor" : "triage";
      tuide::RegistryCausalTriageDecision triage;
      nlohmann::json expanded_payload;
      nlohmann::json* active_payload = nullptr;
      tuide::RegistryCausalJudgeDecision decision;
      bool survey_ran = false;
      bool gold_in_hypotheses = false;
      std::vector<std::string> must_compete;

      if (use_slot_survey) {
        survey_ran = true;
        must_compete = tuide::registry_collect_must_compete_zone_ids(base_payload, 2);
        tuide::RegistrySlotSurveyResult slot_result;
        slot_result.queue = tuide::registry_collect_slot_queue_zone_ids(base_payload, 8);
        std::ofstream(case_out / "must_compete.json")
            << nlohmann::json(must_compete).dump(2) << "\n";
        std::ofstream(case_out / "slot_queue.json")
            << nlohmann::json(slot_result.queue).dump(2) << "\n";

        auto expected_stems_vec = [&]() {
          std::vector<std::string> out;
          for (const auto& value : item.value("expected_stems", nlohmann::json::array())) {
            if (value.is_string()) {
              out.push_back(value.get<std::string>());
            }
          }
          return out;
        }();

        for (size_t si = 0; si < slot_result.queue.size(); ++si) {
          const std::string primary_id = slot_result.queue[si];
          const auto supporting =
              tuide::registry_slot_supporting_zone_ids(base_payload, primary_id, 2);
          const std::string slot_cards =
              tuide::registry_causal_slot_cards_markdown(base_payload, primary_id, supporting);
          const std::string prefix = "slot" + std::to_string(si) + "_" + primary_id;
          std::ofstream(case_out / (prefix + "_cards.md")) << slot_cards;

          auto run_slot = [&](const std::string& retry_need,
                              const std::string& art) -> tuide::RegistrySlotHypothesis {
            tuide::L2BrainRequest req;
            req.system_prompt = tuide::registry_causal_slot_system_prompt();
            req.user_prompt = "## Consulta\n" + instruction + "\n\n" +
                              tuide::registry_causal_slot_user_prompt(slot_cards, primary_id,
                                                                     retry_need);
            req.phase = "causal_zone_slot_hyp";
            req.max_tokens =
                std::min(256, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 256);
            req.n_ctx = std::max(4096, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 4096);
            req.temperature = 0.05f;
            const auto t0 = std::chrono::steady_clock::now();
            const auto response = tuide::propose_with_think(brain, &req, nullptr);
            triage_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
            std::ofstream(case_out / (art + "_system.txt")) << req.system_prompt;
            std::ofstream(case_out / (art + "_user.md")) << req.user_prompt;
            std::ofstream(case_out / (art + "_raw.txt")) << response.text;
            tuide::RegistrySlotHypothesis hyp;
            if (!response.ok) {
              hyp.error = response.error;
              hyp.raw = response.text;
              hyp.primary = primary_id;
              return hyp;
            }
            hyp = tuide::registry_parse_causal_slot_hypothesis(response.text, primary_id,
                                                               allowed_targets);
            hyp.raw = response.text;
            return hyp;
          };

          auto hyp = run_slot("", prefix);
          std::string verr;
          bool vok = tuide::registry_validate_slot_hypothesis(hyp, primary_id, allowed_targets,
                                                              &verr);
          if (!vok) {
            auto retry = run_slot("Slot inválido (" + verr +
                                      "). Emite hyp centrada en " + primary_id +
                                      " o discard con expand_from de esa ficha.",
                                  prefix + "_retry");
            hyp = std::move(retry);
            vok = tuide::registry_validate_slot_hypothesis(hyp, primary_id, allowed_targets,
                                                           &verr);
          }
          if (!vok) {
            tuide::registry_inject_synthetic_slot_hypothesis(&hyp, primary_id, base_payload,
                                                             allowed_targets);
          }
          std::ofstream(case_out / (prefix + ".json"))
              << nlohmann::json({{"primary", hyp.primary},
                                 {"hypothesis", hyp.hypothesis},
                                 {"confidence", hyp.confidence},
                                 {"discard", hyp.discard},
                                 {"discard_reason", hyp.discard_reason},
                                 {"expand_from", hyp.expand_from},
                                 {"synthetic", hyp.synthetic},
                                 {"ok", hyp.ok},
                                 {"error", hyp.error},
                                 {"validated", vok}})
                     .dump(2)
              << "\n";
          slot_result.slots.push_back(std::move(hyp));
        }

        slot_result.retained =
            tuide::registry_slot_retain_hypotheses(slot_result.slots, base_payload, 3);
        slot_result.gold_in_hypotheses = tuide::registry_slot_gold_in_hypotheses(
            slot_result.retained, base_payload, expected_stems_vec);
        gold_in_hypotheses = slot_result.gold_in_hypotheses;
        std::ofstream(case_out / "hyps.json")
            << json_dump_safe(tuide::registry_slot_survey_to_json(slot_result)) << "\n";

        if (slot_result.retained.empty()) {
          rows.push_back({{"id", id},
                          {"ok", false},
                          {"error", "slot_survey_no_hyps"},
                          {"triage_ms", triage_ms},
                          {"slot_survey", true},
                          {"gold_in_hypotheses", false}});
          continue;
        }

        auto threads = tuide::registry_slot_hyps_to_threads(slot_result.retained, base_payload,
                                                            allowed_targets);
        // Fase hyp-gen: expand thin slices; selected = retained primaries (sin synth/falsify).
        nlohmann::json merged_zones = nlohmann::json::array();
        std::unordered_set<std::string> seen_zone;
        std::vector<std::string> retained_primaries;
        std::string combined_hyp;
        for (size_t ti = 0; ti < threads.size(); ++ti) {
          auto thread_triage = threads[ti];
          tuide::registry_apply_deterministic_co_shortlist(base_payload, &thread_triage);
          const std::string prefix = "thread" + std::to_string(ti) + "_";
          std::ofstream(case_out / (prefix + "anchor.json"))
              << json_dump_safe(tuide::registry_causal_triage_decision_to_json(thread_triage))
              << "\n";
          nlohmann::json th_payload;
          if (!expand_from_decision(thread_triage, &th_payload)) {
            continue;
          }
          std::ofstream(case_out / (prefix + "cards_expanded.json"))
              << json_dump_safe(th_payload) << "\n";
          const std::string th_cards = tuide::registry_causal_judge_markdown(th_payload);
          std::ofstream(case_out / (prefix + "cards_expanded.md")) << th_cards;
          if (!thread_triage.shortlist.empty()) {
            retained_primaries.push_back(thread_triage.shortlist.front());
          }
          if (combined_hyp.empty()) {
            combined_hyp = thread_triage.hypothesis;
          } else {
            combined_hyp += " || " + thread_triage.hypothesis;
          }
          for (const auto& z : th_payload.value("zones", nlohmann::json::array())) {
            const std::string zid = z.value("id", "");
            if (zid.empty() || !seen_zone.insert(zid).second) {
              continue;
            }
            merged_zones.push_back(z);
          }
          if (ti == 0) {
            triage = thread_triage;
          }
        }
        if (retained_primaries.empty()) {
          rows.push_back({{"id", id},
                          {"ok", false},
                          {"error", "slot_expand_failed"},
                          {"triage_ms", triage_ms},
                          {"slot_survey", true},
                          {"gold_in_hypotheses", slot_result.gold_in_hypotheses}});
          continue;
        }
        expanded_payload = base_payload;
        expanded_payload["zones"] = merged_zones;
        active_payload = &expanded_payload;
        cards = tuide::registry_causal_judge_markdown(expanded_payload);
        zone_ids = zone_ids_from_payload(expanded_payload);
        hypothesis = combined_hyp;
        anchor_why = "slot_survey retained=" + std::to_string(retained_primaries.size());
        decision.ok = true;
        decision.selected = retained_primaries;
        decision.next = "verify";
        decision.hypothesis_status = "partial";
        decision.why = hypothesis;
        if (decision.why.size() > 200) {
          decision.why.resize(200);
          while (!decision.why.empty()) {
            const auto c = static_cast<unsigned char>(decision.why.back());
            if ((c & 0xc0) != 0x80) {
              if ((c & 0x80) != 0) {
                decision.why.pop_back();
              }
              break;
            }
            decision.why.pop_back();
          }
        }
        for (const auto& pid : retained_primaries) {
          tuide::RegistryZoneVerdict zv;
          zv.id = pid;
          zv.verdict = "select";
          zv.role = "primary";
          zv.completeness = "partial";
          zv.confidence = 0.55f;
          zv.why = "hyp retenida en slot-survey";
          zv.contribution = zv.why;
          decision.zones.push_back(std::move(zv));
        }
        std::ofstream(case_out / "anchor.json")
            << json_dump_safe(tuide::registry_causal_triage_decision_to_json(triage)) << "\n";
        std::ofstream(case_out / "triage.json")
            << json_dump_safe(tuide::registry_causal_triage_decision_to_json(triage)) << "\n";
        std::ofstream(case_out / "cards_expanded.json") << json_dump_safe(expanded_payload)
                                                         << "\n";
        std::ofstream(case_out / "cards_expanded.md") << cards;
        std::ofstream(case_out / "survey_winner.json")
            << json_dump_safe(nlohmann::json({{"retained_primaries", retained_primaries},
                                              {"gold_in_hypotheses", slot_result.gold_in_hypotheses},
                                              {"must_compete", must_compete},
                                              {"queue", slot_result.queue}}))
            << "\n";
      } else if (use_primary_survey) {
        survey_ran = true;
        must_compete = tuide::registry_collect_must_compete_zone_ids(base_payload, 2);
        const std::string survey_cards =
            tuide::registry_strip_zone_scores_markdown(triage_cards);
        std::ofstream(case_out / "must_compete.json")
            << nlohmann::json(must_compete).dump(2) << "\n";

        auto run_contrast_llm =
            [&](const std::string& retry_need,
                const std::string& artifact_prefix) -> tuide::RegistryContrastDecision {
          tuide::L2BrainRequest req;
          req.system_prompt = tuide::registry_causal_contrast_system_prompt();
          req.user_prompt = "## Consulta\n" + instruction + "\n\n" +
                            tuide::registry_causal_contrast_user_prompt(survey_cards, must_compete,
                                                                       retry_need);
          req.phase = "causal_zone_contrast";
          req.max_tokens =
              std::min(512, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 512);
          req.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
          req.temperature = 0.05f;
          const auto t0 = std::chrono::steady_clock::now();
          const auto response = tuide::propose_with_think(brain, &req, nullptr);
          triage_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - t0)
                           .count();
          std::ofstream(case_out / (artifact_prefix + "_system.txt")) << req.system_prompt;
          std::ofstream(case_out / (artifact_prefix + "_user.md")) << req.user_prompt;
          std::ofstream(case_out / (artifact_prefix + "_raw.txt")) << response.text;
          tuide::RegistryContrastDecision contrast;
          if (!response.ok) {
            contrast.error = response.error;
            contrast.raw = response.text;
            return contrast;
          }
          contrast = tuide::registry_parse_causal_contrast(response.text, triage_ids,
                                                           allowed_targets);
          std::ofstream(case_out / (artifact_prefix + ".json"))
              << tuide::registry_contrast_to_json(contrast).dump(2) << "\n";
          return contrast;
        };

        auto contrast = run_contrast_llm("", "contrast");
        auto validation = tuide::registry_validate_contrast_threads(
            contrast, must_compete, base_payload, allowed_targets);
        if (!validation.ok &&
            (validation.error == "incomplete_contrast" ||
             validation.error == "duplicate_hypothesis" || validation.error == "empty_threads" ||
             !contrast.ok)) {
          std::ostringstream retry_need;
          retry_need << "Contraste incompleto (" << validation.error << "). ";
          if (!must_compete.empty()) {
            retry_need << "Debes emitir thread primary=";
            for (size_t i = 0; i < must_compete.size(); ++i) {
              retry_need << (i ? "|" : "") << must_compete[i];
            }
            retry_need << " con mecanismo anclado a stems/targets de esa zona, "
                          "o discard válido con expand_from de esa ficha.";
          } else {
            retry_need << "Emite 1–2 hyp incompatibles sin copiar la misma frase.";
          }
          auto retry = run_contrast_llm(retry_need.str(), "contrast_retry");
          if (retry.ok) {
            contrast = std::move(retry);
          }
          validation = tuide::registry_validate_contrast_threads(
              contrast, must_compete, base_payload, allowed_targets);
        }
        if (!validation.ok && !must_compete.empty()) {
          tuide::registry_inject_synthetic_contrast_threads(&contrast, must_compete, base_payload,
                                                            allowed_targets);
          validation = tuide::registry_validate_contrast_threads(
              contrast, must_compete, base_payload, allowed_targets);
        }
        std::ofstream(case_out / "survey_cards.md") << survey_cards;
        std::ofstream(case_out / "survey.json")
            << tuide::registry_contrast_to_json(contrast).dump(2) << "\n";
        std::ofstream(case_out / "contrast_validation.json")
            << nlohmann::json({{"ok", validation.ok},
                               {"error", validation.error},
                               {"must_compete", must_compete},
                               {"injected", contrast.injected}})
                   .dump(2)
            << "\n";
        if (!contrast.ok || contrast.threads.empty()) {
          rows.push_back({{"id", id},
                          {"ok", false},
                          {"error", contrast.error.empty() ? validation.error : contrast.error},
                          {"triage_ms", triage_ms},
                          {"primary_survey", true}});
          continue;
        }
        auto threads = tuide::registry_contrast_select_threads(contrast, base_payload,
                                                               allowed_targets, 2);
        if (threads.empty()) {
          rows.push_back({{"id", id},
                          {"ok", false},
                          {"error", "survey_no_threads"},
                          {"triage_ms", triage_ms},
                          {"primary_survey", true}});
          continue;
        }
        struct ThreadResult {
          tuide::RegistryCausalTriageDecision triage;
          tuide::RegistryCausalJudgeDecision decision;
          nlohmann::json payload;
          std::string cards;
          std::vector<std::string> zone_ids;
          float survey_confidence = 0.f;
          std::string primary_id;
        };
        std::vector<ThreadResult> thread_results;
        for (size_t ti = 0; ti < threads.size(); ++ti) {
          auto thread_triage = threads[ti];
          tuide::registry_apply_deterministic_co_shortlist(base_payload, &thread_triage);
          const std::string prefix = "thread" + std::to_string(ti) + "_";
          std::ofstream(case_out / (prefix + "anchor.json"))
              << tuide::registry_causal_triage_decision_to_json(thread_triage).dump(2) << "\n";
          ThreadResult tr;
          tr.triage = thread_triage;
          tr.primary_id = thread_triage.shortlist.empty() ? "" : thread_triage.shortlist.front();
          for (const auto& th : contrast.threads) {
            if (th.primary == tr.primary_id) {
              tr.survey_confidence = th.confidence;
              break;
            }
          }
          if (!expand_from_decision(thread_triage, &tr.payload)) {
            continue;
          }
          std::ofstream(case_out / (prefix + "cards_expanded.json")) << tr.payload.dump(2) << "\n";
          tr.cards = tuide::registry_causal_judge_markdown(tr.payload);
          tr.zone_ids = zone_ids_from_payload(tr.payload);
          std::ofstream(case_out / (prefix + "cards_expanded.md")) << tr.cards;
          tr.decision =
              run_synth(tr.cards, tr.zone_ids, thread_triage.hypothesis, thread_triage.why, prefix);
          tuide::registry_apply_synth_hypothesis_tiebreak(tr.payload, thread_triage.hypothesis,
                                                          thread_triage.why, &tr.decision);
          thread_results.push_back(std::move(tr));
        }
        if (thread_results.empty()) {
          rows.push_back({{"id", id},
                          {"ok", false},
                          {"error", "survey_threads_expand_failed"},
                          {"triage_ms", triage_ms},
                          {"primary_survey", true}});
          continue;
        }
        auto synth_rank = [](const tuide::RegistryCausalJudgeDecision& d) -> int {
          if (!d.ok) {
            return -1;
          }
          if (d.hypothesis_status == "confirmed") {
            return 3;
          }
          if (d.hypothesis_status == "partial") {
            return 2;
          }
          if (d.hypothesis_status == "falsified") {
            return 0;
          }
          return 1;
        };
        auto is_must_primary = [&](const std::string& pid) {
          return std::find(must_compete.begin(), must_compete.end(), pid) != must_compete.end();
        };
        size_t best_i = 0;
        for (size_t i = 1; i < thread_results.size(); ++i) {
          const auto& a = thread_results[best_i];
          const auto& b = thread_results[i];
          const int ra = synth_rank(a.decision);
          const int rb = synth_rank(b.decision);
          const bool b_hits_primary =
              !b.decision.selected.empty() && b.decision.selected.front() == b.primary_id;
          const bool a_hits_primary =
              !a.decision.selected.empty() && a.decision.selected.front() == a.primary_id;
          const bool a_must = is_must_primary(a.primary_id);
          const bool b_must = is_must_primary(b.primary_id);
          if (rb > ra || (rb == ra && b_hits_primary && !a_hits_primary) ||
              (rb == ra && b_hits_primary == a_hits_primary && b_must && !a_must) ||
              (rb == ra && b_hits_primary == a_hits_primary && b_must == a_must &&
               b.survey_confidence > a.survey_confidence + 1e-6f)) {
            best_i = i;
          }
        }
        // No coronar M1-confirmed sin haber evaluado un hilo must-compete si existe.
        if (!must_compete.empty() && thread_results.size() >= 2) {
          const auto& win = thread_results[best_i];
          const bool win_is_must = is_must_primary(win.primary_id);
          if (!win_is_must && win.decision.hypothesis_status == "confirmed") {
            for (size_t i = 0; i < thread_results.size(); ++i) {
              if (!is_must_primary(thread_results[i].primary_id)) {
                continue;
              }
              if (thread_results[i].decision.ok &&
                  (thread_results[i].decision.hypothesis_status == "partial" ||
                   thread_results[i].decision.hypothesis_status == "confirmed")) {
                // Prefer must-compete partial over unrelated confirmed only if must has evidence.
                if (thread_results[i].decision.hypothesis_status == "confirmed" ||
                    (!thread_results[i].decision.selected.empty() &&
                     thread_results[i].decision.selected.front() ==
                         thread_results[i].primary_id)) {
                  best_i = i;
                }
              }
            }
          }
        }
        // Si el ganador falló parse y hay hilo must-compete evaluado, preferirlo (mordida/M7).
        if (!must_compete.empty() && !thread_results[best_i].decision.ok) {
          for (size_t i = 0; i < thread_results.size(); ++i) {
            if (is_must_primary(thread_results[i].primary_id)) {
              best_i = i;
              break;
            }
          }
        }
        auto& winner = thread_results[best_i];
        triage = winner.triage;
        decision = winner.decision;
        expanded_payload = std::move(winner.payload);
        active_payload = &expanded_payload;
        cards = winner.cards;
        zone_ids = winner.zone_ids;
        hypothesis = winner.triage.hypothesis;
        anchor_why = winner.triage.why;
        std::ofstream(case_out / "anchor.json")
            << tuide::registry_causal_triage_decision_to_json(triage).dump(2) << "\n";
        std::ofstream(case_out / "triage.json")
            << tuide::registry_causal_triage_decision_to_json(triage).dump(2) << "\n";
        std::ofstream(case_out / "cards_expanded.json") << expanded_payload.dump(2) << "\n";
        std::ofstream(case_out / "cards_expanded.md") << cards;
        std::ofstream(case_out / "survey_winner.json")
            << nlohmann::json({{"thread_index", best_i},
                               {"primary_id", winner.primary_id},
                               {"survey_confidence", winner.survey_confidence},
                               {"hypothesis_status", decision.hypothesis_status},
                               {"selected", decision.selected},
                               {"must_compete", must_compete},
                               {"injected", contrast.injected}})
                   .dump(2)
            << "\n";
      } else {
        triage = run_anchor_or_triage("", first_prefix);
        if (!triage.ok || triage.shortlist.empty()) {
          rows.push_back({{"id", id},
                          {"ok", false},
                          {"error", triage.ok ? "retrieval_gap" : triage.error},
                          {"retrieval_needed", triage.retrieval_needed},
                          {"triage_ms", triage_ms},
                          {"epistemic", epistemic}});
          continue;
        }
        if (epistemic) {
          tuide::registry_apply_deterministic_co_shortlist(base_payload, &triage);
          std::ofstream(case_out / "anchor.json")
              << tuide::registry_causal_triage_decision_to_json(triage).dump(2) << "\n";
          std::ofstream(case_out / "triage.json")
              << tuide::registry_causal_triage_decision_to_json(triage).dump(2) << "\n";
        }
        // Legacy padding a 3 zonas solo fuera del modo epistémico.
        if (!epistemic && triage.ok && triage.shortlist.size() < 3) {
          std::unordered_set<std::string> selected(triage.shortlist.begin(),
                                                   triage.shortlist.end());
          std::unordered_set<std::string> selected_context;
          for (const auto& zone : base_payload["zones"]) {
            if (!selected.count(zone.value("id", ""))) {
              continue;
            }
            for (const auto& stem : zone.value("context_stems", nlohmann::json::array())) {
              if (stem.is_string()) {
                selected_context.insert(stem.get<std::string>());
              }
            }
          }
          for (const auto& zone : base_payload["zones"]) {
            if (triage.shortlist.size() >= 3) {
              break;
            }
            const std::string candidate = zone.value("id", "");
            if (candidate.empty() || selected.count(candidate)) {
              continue;
            }
            bool linked_context = false;
            for (const auto& stem : zone.value("primary_stems", nlohmann::json::array())) {
              linked_context =
                  linked_context ||
                  (stem.is_string() && selected_context.count(stem.get<std::string>()) > 0);
            }
            const auto targets_it = allowed_targets.find(candidate);
            if (!linked_context || targets_it == allowed_targets.end()) {
              continue;
            }
            auto target_it = std::find_if(targets_it->second.begin(), targets_it->second.end(),
                                          [&](const std::string& target) {
                                            return target_matches_query(target, instruction);
                                          });
            if (target_it == targets_it->second.end()) {
              continue;
            }
            tuide::RegistryZoneTriage complement;
            complement.id = candidate;
            complement.verdict = "inspect";
            complement.need = "comprobar brazo causal complementario enlazado por contexto";
            complement.expand_from = {*target_it};
            triage.zones.push_back(std::move(complement));
            triage.shortlist.push_back(candidate);
            selected.insert(candidate);
          }
          std::ofstream(case_out / "triage.json")
              << tuide::registry_causal_triage_decision_to_json(triage).dump(2) << "\n";
        }

        hypothesis = triage.hypothesis;
        anchor_why = triage.why;
        if (!expand_from_decision(triage, &expanded_payload)) {
          rows.push_back({{"id", id}, {"ok", false}, {"error", err}, {"triage_ms", triage_ms}});
          continue;
        }
        std::ofstream(case_out / "cards_expanded.json") << expanded_payload.dump(2) << "\n";
        cards = tuide::registry_causal_judge_markdown(expanded_payload);
        zone_ids = zone_ids_from_payload(expanded_payload);
        std::ofstream(case_out / "cards_expanded.md") << cards;
        active_payload = &expanded_payload;

        decision = run_synth(cards, zone_ids, hypothesis.empty() ? instruction : hypothesis,
                             anchor_why, "");
        if (epistemic && decision.ok && decision.next == "reinvestigate" && !reopened) {
          reopened = true;
          auto reopen = run_anchor_or_triage(decision.reinvestigate_need, "reopen_anchor");
          if (reopen.ok && !reopen.shortlist.empty()) {
            tuide::registry_apply_deterministic_co_shortlist(base_payload, &reopen);
            std::ofstream(case_out / "reopen_anchor.json")
                << tuide::registry_causal_triage_decision_to_json(reopen).dump(2) << "\n";
            hypothesis = reopen.hypothesis;
            anchor_why = reopen.why;
            nlohmann::json reopen_payload;
            if (expand_from_decision(reopen, &reopen_payload)) {
              std::ofstream(case_out / "cards_reopened.json") << reopen_payload.dump(2) << "\n";
              cards = tuide::registry_causal_judge_markdown(reopen_payload);
              zone_ids = zone_ids_from_payload(reopen_payload);
              std::ofstream(case_out / "cards_reopened.md") << cards;
              expanded_payload = std::move(reopen_payload);
              active_payload = &expanded_payload;
              decision = run_synth(cards, zone_ids, hypothesis, anchor_why, "reopen_");
            }
          }
        }
      }
      (void)survey_ran;
      // Si tras anclar/falsar no queda select, conservar la apuesta expandida (mordida mínima).
      if (epistemic && decision.selected.empty() && !zone_ids.empty()) {
        std::string keep = zone_ids.front();
        // Tras contraste: preferir must-compete presente en el thin slice (p.ej. M7).
        for (const auto& mid : must_compete) {
          if (std::find(zone_ids.begin(), zone_ids.end(), mid) != zone_ids.end()) {
            keep = mid;
            break;
          }
        }
        bool touched = false;
        for (auto& zone : decision.zones) {
          if (zone.id != keep) {
            continue;
          }
          zone.verdict = "select";
          zone.role = "primary";
          zone.completeness = "complete";
          zone.confidence = std::max(0.55f, zone.confidence);
          if (zone.why.size() < 12) {
            zone.why = !hypothesis.empty() ? hypothesis : anchor_why;
          }
          if (zone.why.size() < 12) {
            zone.why = "ancla expandida conservada tras síntesis vacía";
          }
          zone.contribution = zone.why.size() > 140 ? zone.why.substr(0, 140) : zone.why;
          zone.missing_link.clear();
          zone.expand_from.clear();
          touched = true;
        }
        if (!touched) {
          tuide::RegistryZoneVerdict zone;
          zone.id = keep;
          zone.verdict = "select";
          zone.role = "primary";
          zone.completeness = "complete";
          zone.confidence = 0.55f;
          zone.why = !hypothesis.empty() ? hypothesis : "ancla expandida conservada tras síntesis vacía";
          if (zone.why.size() < 12) {
            zone.why = "ancla expandida conservada tras síntesis vacía";
          }
          zone.contribution = zone.why.size() > 140 ? zone.why.substr(0, 140) : zone.why;
          decision.zones.push_back(std::move(zone));
        }
        for (auto& zone : decision.zones) {
          if (zone.id == keep) {
            continue;
          }
          if (zone.verdict == "select") {
            zone.verdict = "reject";
            zone.role = "none";
            zone.completeness = "none";
            zone.contribution.clear();
            zone.missing_link.clear();
            zone.expand_from.clear();
            if (zone.why.size() < 12) {
              zone.why = "desplazada por ancla conservada";
            }
          }
        }
        decision.selected = {keep};
        decision.next = "verify";
        decision.hypothesis_status = "partial";
        decision.reinvestigate_need.clear();
        decision.error.clear();
        decision.ok = true;
        decision.why = !hypothesis.empty() ? hypothesis : ("conservar ancla " + keep);
        if (decision.why.size() < 12) {
          decision.why = "conservar ancla expandida como mordida mínima";
        }
      }
      if (epistemic && active_payload != nullptr) {
        tuide::registry_apply_synth_hypothesis_tiebreak(
            *active_payload, hypothesis.empty() ? instruction : hypothesis, anchor_why,
            &decision);
      }
      // Escribir decision final canónica y continuar al scoring del bucle (sin segundo propose).
      std::ofstream(case_out / "system.txt") << system;
      std::ofstream(case_out / "cards.md") << cards;
      const nlohmann::json decision_json =
          tuide::registry_causal_judge_decision_to_json(decision);
      try {
        std::ofstream(case_out / "decision.json") << decision_json.dump(2) << "\n";
      } catch (const std::exception&) {
        std::ofstream(case_out / "decision.json")
            << nlohmann::json({{"ok", decision.ok},
                               {"action", "causal_zone_judge"},
                               {"selected", decision.selected},
                               {"next", decision.next},
                               {"hypothesis_status", decision.hypothesis_status},
                               {"error", decision.error.empty() ? "utf8_sanitize" : decision.error},
                               {"why", "decision serializada sin why UTF-8 inválido"}})
                   .dump(2)
            << "\n";
      }
      if (!decision.raw.empty()) {
        std::ofstream(case_out / "model_raw.txt") << decision.raw;
      }

      std::unordered_map<std::string, std::unordered_set<std::string>> stems_by_zone;
      std::istringstream cards_in(cards);
      std::string line;
      std::string current_zone;
      while (std::getline(cards_in, line)) {
        if (line.rfind("## M", 0) == 0) {
          const auto end = line.find(' ', 3);
          current_zone =
              line.substr(3, end == std::string::npos ? std::string::npos : end - 3);
        } else if (!current_zone.empty() && line.rfind("stems:", 0) == 0) {
          std::istringstream stem_in(line.substr(6));
          std::string stem;
          while (stem_in >> stem) {
            stems_by_zone[current_zone].insert(stem);
          }
        }
      }
      std::unordered_set<std::string> selected_stems;
      for (const auto& selected : decision.selected) {
        auto sit = stems_by_zone.find(selected);
        if (sit != stems_by_zone.end()) {
          selected_stems.insert(sit->second.begin(), sit->second.end());
        }
      }
      auto fixture_stems = [](const nlohmann::json& values) {
        std::vector<std::string> out;
        if (values.is_array()) {
          for (const auto& value : values) {
            if (value.is_string()) {
              out.push_back(value.get<std::string>());
            }
          }
        }
        return out;
      };
      const auto expected = fixture_stems(item.value("expected_stems", nlohmann::json::array()));
      auto operational = expected;
      if (id == "13_lsp_auto_restart") {
        operational.push_back("lsp_symbol_provider");
      } else if (id == "20_cancel_ai_generation") {
        operational.push_back("busy_strip");
      }
      const auto traps = fixture_stems(item.value("trap_stems", nlohmann::json::array()));
      auto any_stem = [&](const std::vector<std::string>& needles) {
        return std::any_of(needles.begin(), needles.end(),
                           [&](const std::string& stem) { return selected_stems.count(stem) > 0; });
      };
      const bool row_hit = decision.ok && any_stem(expected);
      const bool row_op_hit = decision.ok && any_stem(operational);
      const bool row_trap = decision.ok && any_stem(traps);
      valid += decision.ok ? 1 : 0;
      hit += row_hit ? 1 : 0;
      operational_hit += row_op_hit ? 1 : 0;
      trap += row_trap ? 1 : 0;
      nlohmann::json selected_stems_json = nlohmann::json::array();
      for (const auto& stem : selected_stems) {
        selected_stems_json.push_back(stem);
      }
      rows.push_back({{"id", id},
                      {"ok", decision.ok},
                      {"error", decision.error},
                      {"selected", decision.selected},
                      {"selected_stems", selected_stems_json},
                      {"hit", row_hit},
                      {"operational_hit", row_op_hit},
                      {"trap", row_trap},
                      {"triage_ms", triage_ms},
                      {"reopened", reopened},
                      {"hypothesis_status", decision.hypothesis_status},
                      {"next", decision.next},
                      {"epistemic", epistemic},
                      {"slot_survey", use_slot_survey},
                      {"gold_in_hypotheses", gold_in_hypotheses}});
      std::cout << "==== zone judge " << id << " (" << total << ") ====\n" << std::flush;
      std::cout << "  " << (decision.ok ? "OK" : "INVALID") << " selected=";
      for (const auto& selected : decision.selected) {
        std::cout << selected << " ";
      }
      std::cout << "hit=" << row_hit << " op=" << row_op_hit << " trap=" << row_trap
                << " reopen=" << reopened;
      if (use_slot_survey) {
        std::cout << " gold_in_hyps=" << gold_in_hypotheses;
      }
      std::cout << "\n" << std::flush;
      continue;
    } else {
      const fs::path cards_path = cards_root / id / "judge_cards.md";
      cards = read_file(cards_path);
      zone_ids = tuide::registry_causal_judge_zone_ids(cards);
    }
    if (cards.empty() || zone_ids.empty()) {
      rows.push_back({{"id", id}, {"ok", false}, {"error", "missing_cards"}});
      continue;
    }
    const std::string user =
        "## Consulta\n" + instruction + "\n\n" + tuide::registry_causal_judge_user_prompt(cards);
    tuide::L2BrainRequest request;
    request.system_prompt = system;
    request.user_prompt = user;
    request.phase = "causal_zone_judge";
    const int judge_token_cap = 384;
    request.max_tokens = std::min(
        judge_token_cap,
        settings.level2.max_tokens > 0 ? settings.level2.max_tokens : judge_token_cap);
    request.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
    request.temperature = 0.05f;
    std::cout << "==== zone judge " << id << " (" << total << ") ====\n";
    const auto t0 = std::chrono::steady_clock::now();
    const auto response = tuide::propose_with_think(brain, &request, nullptr);
    const int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - t0)
                                   .count();
    std::ofstream(case_out / "system.txt") << system;
    std::ofstream(case_out / "user.md") << user;
    std::ofstream(case_out / "cards.md") << cards;
    std::ofstream(case_out / "model_raw.txt") << response.text;
    if (!response.ok) {
      rows.push_back(
          {{"id", id}, {"ok", false}, {"error", response.error}, {"elapsed_ms", elapsed_ms}});
      std::cout << "  FAIL backend: " << response.error << "\n";
      continue;
    }
    auto decision = tuide::registry_parse_causal_judge_decision(response.text, zone_ids);
    if (decision.ok) {
      for (const auto& verdict : decision.zones) {
        for (const auto& symbol : verdict.expand_from) {
          if (cards.find(symbol) == std::string::npos) {
            decision.ok = false;
            decision.error =
                "expand_from no pertenece a la ficha " + verdict.id + ": " + symbol;
            break;
          }
        }
        if (!decision.ok) {
          break;
        }
      }
    }
    const nlohmann::json decision_json =
        tuide::registry_causal_judge_decision_to_json(decision);
    std::ofstream(case_out / "decision.json") << decision_json.dump(2) << "\n";

    std::unordered_map<std::string, std::unordered_set<std::string>> stems_by_zone;
    std::istringstream cards_in(cards);
    std::string line;
    std::string current_zone;
    while (std::getline(cards_in, line)) {
      if (line.rfind("## M", 0) == 0) {
        const auto end = line.find(' ', 3);
        current_zone =
            line.substr(3, end == std::string::npos ? std::string::npos : end - 3);
      } else if (!current_zone.empty() && line.rfind("stems:", 0) == 0) {
        std::istringstream stem_in(line.substr(6));
        std::string stem;
        while (stem_in >> stem) {
          stems_by_zone[current_zone].insert(stem);
        }
      }
    }
    std::unordered_set<std::string> selected_stems;
    for (const auto& selected : decision.selected) {
      auto sit = stems_by_zone.find(selected);
      if (sit != stems_by_zone.end()) {
        selected_stems.insert(sit->second.begin(), sit->second.end());
      }
    }
    auto fixture_stems = [](const nlohmann::json& values) {
      std::vector<std::string> out;
      if (values.is_array()) {
        for (const auto& value : values) {
          if (value.is_string()) {
            out.push_back(value.get<std::string>());
          }
        }
      }
      return out;
    };
    const auto expected = fixture_stems(item.value("expected_stems", nlohmann::json::array()));
    auto operational = expected;
    if (id == "13_lsp_auto_restart") {
      operational.push_back("lsp_symbol_provider");
    } else if (id == "20_cancel_ai_generation") {
      operational.push_back("busy_strip");
    }
    const auto traps = fixture_stems(item.value("trap_stems", nlohmann::json::array()));
    auto any_stem = [&](const std::vector<std::string>& needles) {
      return std::any_of(needles.begin(), needles.end(),
                         [&](const std::string& stem) { return selected_stems.count(stem) > 0; });
    };
    const bool row_hit = decision.ok && any_stem(expected);
    const bool row_op_hit = decision.ok && any_stem(operational);
    const bool row_trap = decision.ok && any_stem(traps);
    valid += decision.ok ? 1 : 0;
    hit += row_hit ? 1 : 0;
    operational_hit += row_op_hit ? 1 : 0;
    trap += row_trap ? 1 : 0;
    nlohmann::json selected_stems_json = nlohmann::json::array();
    for (const auto& stem : selected_stems) {
      selected_stems_json.push_back(stem);
    }
    rows.push_back({{"id", id},
                    {"ok", decision.ok},
                    {"error", decision.error},
                    {"selected", decision.selected},
                    {"selected_stems", selected_stems_json},
                    {"hit", row_hit},
                    {"operational_hit", row_op_hit},
                    {"trap", row_trap},
                    {"elapsed_ms", elapsed_ms}});
    std::cout << "  " << (decision.ok ? "OK" : "INVALID") << " selected=";
    for (const auto& selected : decision.selected) {
      std::cout << selected << " ";
    }
    std::cout << "hit=" << row_hit << " op=" << row_op_hit << " trap=" << row_trap
              << " ms=" << elapsed_ms << "\n";
  }
  const nlohmann::json summary = {{"total", total},
                                  {"valid", valid},
                                  {"hit", hit},
                                  {"operational_hit", operational_hit},
                                  {"trap", trap}};
  std::ofstream(output_root / "results.json") << rows.dump(2) << "\n";
  std::ofstream(output_root / "summary.json") << summary.dump(2) << "\n";
  std::cout << "==== summary ====\n" << summary.dump(2) << "\n";
  if (two_pass) {
    tuide::registry_close(&expansion_registry);
  }
  return valid == total ? 0 : 1;
}

// One LLM turn: inject a trail pack for a supplied L0 and ask a_trail_judge.
int run_trail_judge_shot(ToolRegistry* tools, const std::string& root, int argc, char** argv) {
  std::string sym;
  std::string path_hint;
  std::string instruction;
  std::string gold_needle;
  std::string out_dir;
  bool dry = false;
  bool do_suspect = true;
  bool require_gold = false;
  std::string gold_var;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--path" && i + 1 < argc) {
      path_hint = argv[++i];
    } else if (a == "--instruction" && i + 1 < argc) {
      instruction = argv[++i];
    } else if (a == "--case" && i + 1 < argc) {
      // Load prompt from stem_boost battery JSON by id
      const std::string case_id = argv[++i];
      const fs::path prompts =
          fs::path(root) / "tests/fixtures/stem_boost_battery/prompts_nl_human.json";
      std::ifstream in(prompts);
      if (!in) {
        std::cerr << "trail-judge-shot: no se pudo leer " << prompts << "\n";
        return 2;
      }
      nlohmann::json arr;
      in >> arr;
      bool found = false;
      for (const auto& c : arr) {
        if (c.value("id", "") == case_id) {
          instruction = c.value("prompt", "");
          found = true;
          break;
        }
      }
      if (!found || instruction.empty()) {
        std::cerr << "trail-judge-shot: case id no encontrado: " << case_id << "\n";
        return 2;
      }
    } else if (a == "--gold" && i + 1 < argc) {
      gold_needle = argv[++i];
      require_gold = true;
    } else if (a == "--gold-var" && i + 1 < argc) {
      gold_var = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (a == "--dry") {
      dry = true;
    } else if (a == "--no-suspect") {
      do_suspect = false;
    } else if (a == "--suspect") {
      do_suspect = true;
    } else if (a == "--no-gold-abort") {
      require_gold = false;
    } else if (a == "-h" || a == "--help") {
      std::cerr
          << "trail-judge-shot [SYM] [--path hint] [--case ID|--instruction TEXT]\n"
             "                 [--gold NEEDLE] [--gold-var VARIABLE]\n"
             "                 [--out DIR] [--dry] [--suspect|--no-suspect]\n"
             "                 [--no-gold-abort]\n"
             "  1) trail → a_trail_judge  2) si interesting → ¿variable crítica?\n"
             "     → dataflow-probe rg de las pistas (reserva para Phase B).\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      sym = a;
    }
  }
  if (sym.empty() || instruction.empty()) {
    std::cerr << "trail-judge-shot: requiere SYM y --case ID o --instruction TEXT\n";
    return 2;
  }

  auto search_fn = [&](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
    return harness_search_symbol(tools, root, symbol);
  };

  const auto stacks = tuide::a_trail_build_full_stacks(root, sym, path_hint, search_fn,
                                                       tuide::kATrailMaxStacks,
                                                       tuide::kATrailMaxDepth);
  if (stacks.empty()) {
    std::cerr << "trail-judge-shot: sin stacks para `" << sym << "`\n";
    return 1;
  }

  std::vector<std::string> seeds = {sym};

  tuide::ATrail tr;
  tr.active = true;
  tr.awaiting_judge = true;
  tr.root_anchor = path_hint.empty() ? sym : (path_hint + ":" + sym);
  tr.focus_anchor = tr.root_anchor;
  tr.focus_symbol = sym;
  tr.root_stem = sym;
  tr.root_why = "hipótesis useful del mapa";
  tr.pending_stacks = stacks;
  tr.cond_branches = tuide::a_trail_build_cond_branches(root, sym, path_hint, seeds, search_fn,
                                                        stacks);
  tuide::ATrailHop root_hop;
  root_hop.symbol = sym;
  root_hop.anchor = tr.root_anchor;
  root_hop.path = path_hint;
  root_hop.summary = "L0 map target";
  tr.trail.push_back(root_hop);

  const std::string trail_md = tuide::a_trail_stacks_markdown(tr);

  // Which stack ids contain the gold needle (for scoring)?
  std::vector<std::string> gold_stack_ids;
  if (!gold_needle.empty()) {
    for (const auto& s : stacks) {
      bool hit = false;
      for (const auto& h : s.hops) {
        if (h.symbol.find(gold_needle) != std::string::npos ||
            h.scope_chain.find(gold_needle) != std::string::npos ||
            h.signature.find(gold_needle) != std::string::npos ||
            h.anchor.find(gold_needle) != std::string::npos ||
            h.snippet.find(gold_needle) != std::string::npos) {
          hit = true;
          break;
        }
      }
      if (hit) {
        gold_stack_ids.push_back(s.id);
      }
    }
  }

  std::ostringstream user;
  user << "phase=explore_a step=1 workflow=autofix\n\n";
  user << "## Instruction\n" << instruction << "\n\n";
  user << "## Situación (inyección de prueba)\n"
          "El mapa señaló el target `"
       << sym << "` (`" << path_hint
       << "`) como hipótesis **useful**.\n"
          "El runtime muestra **un** juego de ids (S* o ON/CXL/OFF, no ambos). "
          "`a_trail_judge` solo sobre esos ids. Reject ruido (reindex/outline).\n"
          "Si ves el edit site, `a_done` (≤2 primary).\n\n";
  user << trail_md;

  const std::string system =
      "Eres el Nivel 2 en fase explore_a (localización + trail).\n"
      "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
      "PROHIBIDO action=plan, tool, edit, done next=edit.\n"
      "Objetivo: encontrar el EDIT SITE del síntoma de ## Instruction.\n"
      "Tras useful el runtime muestra call-stacks S* o ramas ON/CXL/OFF (un juego). "
      "SOLO a_trail_judge sobre ids del prompt.\n"
      "Ejemplo: {\"action\":\"a_trail_judge\",\"verdicts\":["
      "{\"target\":\"S1\",\"verdict\":\"interesting\",\"why\":\"caller del síntoma\"},"
      "{\"target\":\"S2\",\"verdict\":\"reject\",\"why\":\"otro feature\"}]}\n"
      "verdict EXACTAMENTE \"interesting\" o \"reject\". interesting ≤3. "
      "Si TODOS reject → L0 se invalida.\n"
      "a_done solo cuando un hop es el edit site (≤2 primary).\n";

  std::cout << "======== trail-judge-shot ========\n";
  std::cout << "L0=" << tr.root_anchor << " stacks=" << stacks.size();
  if (require_gold) {
    std::cout << " gold_needle=`" << gold_needle << "` gold_in=";
    if (gold_stack_ids.empty()) {
      std::cout << "(ninguna pila — el pack no contiene `" << gold_needle << "`)";
      std::cout << " abort\n";
      return 1;
    } else {
      for (std::size_t i = 0; i < gold_stack_ids.size(); ++i) {
        if (i) {
          std::cout << ",";
        }
        std::cout << gold_stack_ids[i];
      }
    }
  }
  std::cout << "\n";
  std::cout << "prompt_chars system=" << system.size() << " user=" << user.str().size() << "\n";

  if (!out_dir.empty()) {
    fs::create_directories(out_dir);
    {
      std::ofstream o(fs::path(out_dir) / "system.txt");
      o << system;
    }
    {
      std::ofstream o(fs::path(out_dir) / "user.md");
      o << user.str();
    }
    {
      std::ofstream o(fs::path(out_dir) / "trail.md");
      o << trail_md;
    }
  }

  if (dry) {
    std::cout << "\n--- user.md (dry) ---\n" << user.str() << "\n";
    std::cout << "dry: no LLM\n";
    return 0;
  }

  const AiSettings settings = load_ai_settings(root);
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  auto brain_ptr = ready_l2_brain(settings, progress, &err);
  if (!brain_ptr) {
    std::cerr << "trail-judge-shot: " << err << "\n";
    return err.find("mode") != std::string::npos ? 2 : 1;
  }
  tuide::L2Brain& brain = *brain_ptr;

  tuide::L2BrainRequest breq;
  breq.system_prompt = system;
  breq.user_prompt = user.str();
  breq.phase = "explore_a";
  breq.max_tokens = settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 1024;
  breq.n_ctx = settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192;
  breq.temperature = 0.1f;

  std::cout << "L2 ▸ trail-judge-shot pidiendo a_trail_judge (" << brain.name() << ")…\n";
  const auto bres = tuide::propose_with_think(brain, &breq, nullptr);
  if (!bres.ok) {
    std::cerr << "LLM FAIL: " << bres.error << "\n";
    return 1;
  }
  if (!out_dir.empty()) {
    std::ofstream o(fs::path(out_dir) / "model_raw.txt");
    o << bres.text;
  }
  std::cout << "\n--- model ---\n" << bres.text << "\n---\n";

  const tuide::L2Action action = tuide::parse_l2_action(bres.text);
  std::vector<std::string> interesting_ids;
  std::vector<std::string> reject_ids;
  bool gold_interesting = false;
  bool gold_cond_interesting = false;
  if (action.kind == tuide::L2ActionKind::ATrailJudge ||
      action.name == "a_trail_judge" || action.name == "trail_judge") {
    for (const auto& v : action.a_verdicts) {
      if (v.verdict == tuide::AVerdictKind::Interesting) {
        interesting_ids.push_back(v.target);
        for (const auto& g : gold_stack_ids) {
          if (v.target == g) {
            gold_interesting = true;
          }
        }
        if (v.target == "LINK" || v.target == "CXL" || v.target == "link" ||
            v.target == "cxl") {
          gold_cond_interesting = true;
        }
      } else if (v.verdict == tuide::AVerdictKind::Reject) {
        reject_ids.push_back(v.target);
      }
    }
  } else if (action.kind == tuide::L2ActionKind::ADone || action.name == "a_done") {
    // Bonus path: model jumps to a_done naming the supplied gold.
    for (const auto& loc : action.a_loci) {
      if (!gold_needle.empty() &&
          (loc.anchor.find(gold_needle) != std::string::npos ||
           loc.stem.find(gold_needle) != std::string::npos)) {
        gold_interesting = true;
      }
    }
    std::cout << "note: model emitted a_done (no a_trail_judge)\n";
  } else {
    std::cout << "WARN: action kind=" << static_cast<int>(action.kind) << " name=" << action.name
              << " error=" << action.error << "\n";
  }

  auto join = [](const std::vector<std::string>& xs) {
    std::ostringstream o;
    for (std::size_t i = 0; i < xs.size(); ++i) {
      if (i) {
        o << ",";
      }
      o << xs[i];
    }
    return o.str();
  };

  std::cout << "interesting=[" << join(interesting_ids) << "] reject=[" << join(reject_ids)
            << "]\n";
  std::cout << "gold_stacks=[" << join(gold_stack_ids) << "] gold_stack="
            << (gold_interesting ? 1 : 0) << " gold_cond=" << (gold_cond_interesting ? 1 : 0)
            << "\n";
  const bool gold_pass = !require_gold || gold_interesting || gold_cond_interesting;
  if (!gold_pass) {
    if (!require_gold && !interesting_ids.empty()) {
      std::cout << "WARN: gold trail miss — continúa con interesting=[" << join(interesting_ids)
                << "]\n";
    } else {
      std::cout << "FAIL: no marcó interesting stack(`" << gold_needle
                << "`) ni rama cond LINK/CXL\n";
      return 1;
    }
  } else {
    if (gold_cond_interesting) {
      std::cout << "PASS: rama condicional cancel/async (LINK/CXL)\n";
    }
    if (gold_interesting) {
      std::cout << "PASS: pila con gold suministrado (" << gold_needle << ")\n";
    }
  }

  if (!do_suspect) {
    return gold_pass || !require_gold ? 0 : 1;
  }

  // --- Phase 2: critical variable? (compact; reserve for B) --------------------
  std::ostringstream focus_md;
  for (const auto& id : interesting_ids) {
    for (const auto& b : tr.cond_branches) {
      if (b.id != id) {
        continue;
      }
      focus_md << "### cond `" << b.id << "`\n";
      if (!b.when_text.empty()) {
        focus_md << "when: `" << b.when_text << "`\n";
      }
      if (!b.then_text.empty()) {
        focus_md << "then: `" << b.then_text << "`\n";
      }
      if (!b.note.empty()) {
        focus_md << "note: " << b.note << "\n";
      }
      if (!b.snippet.empty()) {
        focus_md << "```\n" << b.snippet;
        if (b.snippet.back() != '\n') {
          focus_md << '\n';
        }
        focus_md << "```\n\n";
      }
    }
  }
  for (const auto& s : stacks) {
    bool keep = false;
    for (const auto& id : interesting_ids) {
      if (s.id == id) {
        keep = true;
        break;
      }
    }
    if (!keep) {
      continue;
    }
    focus_md << "### `" << s.id << "`\n";
    if (!s.hops.empty()) {
      const auto& key = s.hops.front();
      focus_md << "caller `" << key.anchor << "` line=" << key.call_line << "\n";
      if (!key.signature.empty()) {
        focus_md << "sig: `" << key.signature << "`\n";
      }
      if (!key.control_cond.empty()) {
        focus_md << "cond: `" << key.control_cond << "`\n";
      }
      focus_md << "```\n" << key.snippet;
      if (!key.snippet.empty() && key.snippet.back() != '\n') {
        focus_md << '\n';
      }
      focus_md << "```\n";
    }
  }

  const std::string sys2 =
      "Eres el Nivel 2 (explore_a). JSON only. PROHIBIDO markdown fuera del JSON.\n"
      "Ya marcaste pilas interesting. Ahora: ¿hay una VARIABLE/CAMPO de estado "
      "crítica para el síntoma de Instruction?\n"
      "Responde EXACTAMENTE:\n"
      "{\"action\":\"a_suspect_vars\",\"vars\":[{\"name\":\"campo_\","
      "\"why\":\"estado en el snippet\",\"anchor\":\"path:Symbol\"}],\"none\":false}\n"
      "o {\"action\":\"a_suspect_vars\",\"vars\":[],\"none\":true}\n"
      "Reglas: máx 2 vars; nombre C++ real del snippet; "
      "si no estás seguro → none:true. No inventes paths.\n";

  std::ostringstream user2;
  user2 << "## Instruction\n" << instruction << "\n\n";
  user2 << "## Pilas interesting (contexto)\n" << focus_md.str() << "\n";
  user2 << "Pregunta: en ese código, ¿qué variable/campo controla el síntoma de Instruction? "
           "Emite a_suspect_vars.\n";

  if (!out_dir.empty()) {
    std::ofstream o(fs::path(out_dir) / "suspect_user.md");
    o << user2.str();
    std::ofstream s2(fs::path(out_dir) / "suspect_system.txt");
    s2 << sys2;
  }

  std::cout << "\n======== suspect-vars shot ========\n";
  std::cout << "prompt_chars system=" << sys2.size() << " user=" << user2.str().size() << "\n";
  std::cout << "L2 ▸ pidiendo a_suspect_vars (" << brain.name() << ")…\n";

  tuide::L2BrainRequest breq2;
  breq2.system_prompt = sys2;
  breq2.user_prompt = user2.str();
  breq2.phase = "explore_a";
  breq2.max_tokens = settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 512;
  breq2.n_ctx = settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192;
  breq2.temperature = 0.1f;
  const auto bres2 = tuide::propose_with_think(brain, &breq2, nullptr);
  if (!bres2.ok) {
    std::cerr << "LLM FAIL (suspect): " << bres2.error << "\n";
    return 1;
  }
  if (!out_dir.empty()) {
    std::ofstream o(fs::path(out_dir) / "suspect_raw.txt");
    o << bres2.text;
  }
  std::cout << "\n--- model (suspect) ---\n" << bres2.text << "\n---\n";

  auto extract_json = [](const std::string& text) -> nlohmann::json {
    const auto a = text.find('{');
    const auto b = text.rfind('}');
    if (a == std::string::npos || b == std::string::npos || b <= a) {
      return nlohmann::json{};
    }
    try {
      return nlohmann::json::parse(text.substr(a, b - a + 1));
    } catch (...) {
      return nlohmann::json{};
    }
  };

  const nlohmann::json sj = extract_json(bres2.text);
  struct Suspect {
    std::string name;
    std::string why;
    std::string anchor;
  };
  std::vector<Suspect> suspects;
  bool none = false;
  if (sj.is_object()) {
    none = sj.value("none", false);
    if (sj.contains("vars") && sj["vars"].is_array()) {
      for (const auto& v : sj["vars"]) {
        if (!v.is_object()) {
          continue;
        }
        Suspect s;
        s.name = v.value("name", "");
        s.why = v.value("why", "");
        s.anchor = v.value("anchor", "");
        // Strip Class:: prefix noise
        const auto colons = s.name.rfind("::");
        if (colons != std::string::npos) {
          s.name = s.name.substr(colons + 2);
        }
        if (!s.name.empty()) {
          suspects.push_back(std::move(s));
        }
      }
    }
  }

  if (suspects.empty() && none) {
    std::cout << "suspect: none=true (sin variable reservada)\n";
    std::cout << "PASS_TRAIL + NO_SUSPECT\n";
    return 0;
  }
  if (suspects.empty()) {
    std::cout << "FAIL: no se pudo parsear a_suspect_vars\n";
    return 1;
  }

  std::cout << "suspect_vars:\n";
  for (const auto& s : suspects) {
    std::cout << "  - `" << s.name << "`";
    if (!s.anchor.empty()) {
      std::cout << " @ " << s.anchor;
    }
    if (!s.why.empty()) {
      std::cout << " — " << s.why;
    }
    std::cout << "\n";
  }
  if (!out_dir.empty()) {
    nlohmann::json jar = nlohmann::json::array();
    for (const auto& s : suspects) {
      jar.push_back({{"name", s.name}, {"why", s.why}, {"anchor", s.anchor}});
    }
    std::ofstream o(fs::path(out_dir) / "suspect_vars.json");
    o << jar.dump(2) << '\n';
  }

  auto search_df = [&](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
    return harness_search_symbol(tools, root, symbol);
  };

  std::string default_df_hint;
  for (const auto& st : stacks) {
    for (const auto& id : interesting_ids) {
      if (st.id != id || st.hops.empty()) {
        continue;
      }
      const auto& hop = st.hops.front();
      default_df_hint = hop.path.empty() ? tuide::a_path_from_anchor(hop.anchor) : hop.path;
      break;
    }
    if (!default_df_hint.empty()) {
      break;
    }
  }

  auto name_matches_gold = [&](const std::string& n) {
    if (gold_var.empty()) {
      return false;
    }
    if (n == gold_var) {
      return true;
    }
    if (n.find(gold_var) != std::string::npos) {
      return true;
    }
    return false;
  };

  bool any_gold_var = false;
  bool any_df_hits = false;
  for (const auto& s : suspects) {
    if (name_matches_gold(s.name)) {
      any_gold_var = true;
    }
    std::string hint = default_df_hint;
    if (s.anchor.find(':') != std::string::npos) {
      hint = s.anchor.substr(0, s.anchor.find(':'));
    }
    if (!default_df_hint.empty() && !hint.empty()) {
      std::cout << "dataflow scope `" << hint << "` (trail caller)\n";
    }
    const auto report = tuide::a_dataflow_build_with_search(root, s.name, hint, search_df);
    std::cout << "\n" << tuide::a_dataflow_markdown(report);
    if (!out_dir.empty()) {
      std::ofstream o(fs::path(out_dir) / ("dataflow_" + s.name + ".md"));
      o << tuide::a_dataflow_markdown(report);
      std::ofstream j(fs::path(out_dir) / ("dataflow_" + s.name + ".json"));
      j << tuide::a_dataflow_to_json(report).dump(2) << '\n';
    }
    if (!report.writes.empty() || !report.reads.empty() || !report.decls.empty()) {
      any_df_hits = true;
    }
  }

  if (!gold_var.empty()) {
    std::cout << "gold_var=`" << gold_var << "` named_gold=" << (any_gold_var ? 1 : 0) << " ";
  }
  std::cout << "dataflow_hits=" << (any_df_hits ? 1 : 0) << "\n";
  if (any_gold_var && any_df_hits) {
    std::cout << "PASS: pista de variable + data-flow rg extraído (reserva B)\n";
    return 0;
  }
  if (any_df_hits) {
    std::cout << "PASS: data-flow extraído";
    if (!gold_var.empty()) {
      std::cout << " pero var ≠ gold (`" << gold_var << "`)";
    }
    std::cout << "\n";
    return 0;  // still useful for design feedback
  }
  std::cout << "FAIL: sospechosas sin sitios data-flow\n";
  return 1;
}

int run_dataflow_probe(ToolRegistry* tools, const std::string& root, int argc, char** argv) {
  std::vector<std::string> names;
  std::string path_hint;
  bool json_out = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--path" && i + 1 < argc) {
      path_hint = argv[++i];
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "dataflow-probe VAR [VAR…] [--path hint] [--json]\n"
                   "  Sin LLM/LSP. ripgrep + heurística write/read/decl + snippet.\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      names.push_back(a);
    }
  }
  if (names.empty()) {
    std::cerr << "dataflow-probe: falta VAR\n";
    return 2;
  }

  auto search_fn = [&](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
    if (tools == nullptr || !tools->has("search") || symbol.empty()) {
      return {};
    }
    // Prefer src/; fall back to whole workspace
    AiToolResult tr = tools->invoke("search", symbol + " path:src/");
    if (!tr.ok || tr.text.find("(sin hits)") != std::string::npos) {
      tr = tools->invoke("search", symbol);
    }
    if (!tr.ok) {
      return {};
    }
    return parse_rg_hits(tr.text, root);
  };

  int rc = 0;
  for (const auto& name : names) {
    const auto report =
        tuide::a_dataflow_build_with_search(root, name, path_hint, search_fn);
    if (json_out) {
      std::cout << tuide::a_dataflow_to_json(report).dump(2) << '\n';
    } else {
      std::cout << "======== dataflow-probe `" << name << "` ========\n";
      std::cout << tuide::a_dataflow_markdown(report);
    }
    if (report.writes.empty() && report.reads.empty() && report.decls.empty()) {
      std::cerr << "dataflow-probe: sin sitios útiles para `" << name << "`\n";
      rc = 1;
    }
  }
  return rc;
}

std::vector<std::string> parse_seeds_csv(const std::string& csv) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : csv) {
    if (c == ',' || c == ';') {
      const std::string t = trim(cur);
      if (!t.empty()) {
        out.push_back(t);
      }
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  const std::string t = trim(cur);
  if (!t.empty()) {
    out.push_back(t);
  }
  return out;
}

bool load_case_prompt(const std::string& root, const std::string& case_id, std::string* prompt,
                      std::vector<std::string>* expected_stems,
                      std::vector<std::string>* trap_stems = nullptr) {
  if (prompt == nullptr) {
    return false;
  }
  const fs::path prompts = fs::path(root) / "tests/fixtures/stem_boost_battery/prompts_nl_human.json";
  std::ifstream in(prompts);
  if (!in) {
    return false;
  }
  nlohmann::json arr;
  in >> arr;
  for (const auto& c : arr) {
    if (c.value("id", "") != case_id) {
      continue;
    }
    *prompt = c.value("prompt", "");
    if (expected_stems != nullptr && c.contains("expected_stems") && c["expected_stems"].is_array()) {
      for (const auto& s : c["expected_stems"]) {
        if (s.is_string()) {
          expected_stems->push_back(s.get<std::string>());
        }
      }
    }
    if (trap_stems != nullptr && c.contains("trap_stems") && c["trap_stems"].is_array()) {
      for (const auto& s : c["trap_stems"]) {
        if (s.is_string()) {
          trap_stems->push_back(s.get<std::string>());
        }
      }
    }
    return !prompt->empty();
  }
  return false;
}

tuide::AVerdict heuristic_a0_verdict(const tuide::EffectSummary& es, const tuide::AQueueItem& item,
                                     const tuide::EffectSummaryQuality& q) {
  tuide::AVerdict v;
  v.target = item.target;
  v.stem = item.stem;
  auto has_hot = [&](const char* tag) {
    return std::find(es.hot.begin(), es.hot.end(), tag) != es.hot.end();
  };
  const bool ui_hot = has_hot("spinner") || has_hot("wake") || has_hot("ui_event") ||
                      has_hot("busy");
  const bool glue =
      es.roles.size() == 1 && es.roles[0] == "glue" && es.writes.empty() && !ui_hot;
  const bool mutator = !es.writes.empty() ||
                       std::find(es.roles.begin(), es.roles.end(), "mutator") != es.roles.end();

  if (q.seed_hits >= 2 || (q.seed_hits >= 1 && ui_hot) || (q.seed_hits >= 1 && mutator)) {
    v.verdict = tuide::AVerdictKind::Expand;
    v.expand_with = tuide::AExpandModality::Peek;
    if (tuide::a_target_prefers_trail_a0(item.target, &es.writes) ||
        es.nudge.rfind("expand:trail", 0) == 0) {
      v.expand_with = tuide::AExpandModality::Trail;
    } else if (!es.writes.empty() && q.seed_hits >= 1) {
      v.expand_with = tuide::AExpandModality::Dataflow;
      v.suspect_var = es.writes.front();
    } else if (ui_hot && mutator) {
      v.expand_with = tuide::AExpandModality::Trail;
    }
    v.expand_with =
        tuide::a_coerce_a0_expand_modality(item.target, v.expand_with, &es.writes);
    v.why = "heuristic: seed_hits=" + std::to_string(q.seed_hits) +
            (ui_hot ? " ui_hot" : "") + (mutator ? " mutator" : "");
    return v;
  }
  if (glue && q.seed_hits == 0) {
    v.verdict = tuide::AVerdictKind::Reject;
    v.why = "heuristic: glue sin seeds";
    return v;
  }
  if (es.nudge == "likely_noise" || es.nudge == "likely_lsp_trap") {
    v.verdict = tuide::AVerdictKind::Reject;
    v.why = "heuristic: " + es.nudge;
    return v;
  }
  if (q.seed_hits >= 1) {
    v.verdict = tuide::AVerdictKind::Uncertain;
    v.expand_with = tuide::AExpandModality::Peek;
    v.why = "heuristic: seed débil";
    return v;
  }
  v.verdict = tuide::AVerdictKind::Reject;
  v.why = "heuristic: sin señal seed/hot";
  return v;
}

int run_effect_summary_probe(const std::string& root, int argc, char** argv) {
  std::vector<std::string> symbols;
  std::vector<std::string> targets;
  std::string path_hint;
  std::string seeds_csv;
  std::string map_path;
  std::string a_state_path;
  int top_n = 20;
  int tranche_n = tuide::kA0MaxCardsPerTurn;
  bool json_out = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--path" && i + 1 < argc) {
      path_hint = argv[++i];
    } else if (a == "--seeds" && i + 1 < argc) {
      seeds_csv = argv[++i];
    } else if (a == "--from-map" && i + 1 < argc) {
      map_path = argv[++i];
    } else if (a == "--from-a-state" && i + 1 < argc) {
      a_state_path = argv[++i];
    } else if (a == "--top" && i + 1 < argc) {
      top_n = std::atoi(argv[++i]);
    } else if (a == "--tranche" && i + 1 < argc) {
      tranche_n = std::atoi(argv[++i]);
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "effect-summary-probe SYM [SYM…] | --target path:Sym …\n"
                   "  | --from-map map.md [--top N] | --from-a-state a_state.json [--tranche N]\n"
                   "  [--seeds a,b,c] [--path hint] [--json]\n"
                   "  Sin LLM. Genera fichas Effect Summary + métricas de calidad.\n";
      return 2;
    } else if (a == "--target" && i + 1 < argc) {
      targets.push_back(argv[++i]);
    } else if (!a.empty() && a[0] != '-') {
      symbols.push_back(a);
    }
  }

  tuide::EffectSummaryOpts opts;
  opts.seeds = parse_seeds_csv(seeds_csv);
  opts.orphans = opts.seeds;

  std::vector<tuide::AQueueItem> items;
  if (!map_path.empty()) {
    const std::string md = read_file(fs::path(map_path));
    tuide::AQueueMapFilterOpts fopts;
    fopts.want_n = static_cast<std::size_t>(top_n);
    fopts.orphans = parse_seeds_csv(seeds_csv);
    const auto inputs =
        tuide::a_queue_inputs_from_ranked_map_filtered(md, fopts, root);
    for (const auto& in : inputs) {
      tuide::AQueueItem q;
      q.path = in.file;
      q.symbol = in.name;
      q.line = in.line;
      q.stem = in.stem;
      q.score = static_cast<float>(in.score);
      q.map_related = in.map_related;
      q.refs_in = in.refs_in;
      q.body_sem_permille = in.body_sem_permille;
      q.file_rank = in.file_rank;
      q.file_count = in.file_count;
      q.dup_stem = in.dup_stem;
      q.stem_sem_rank = in.stem_sem_rank;
      q.target = in.file + (in.name.empty() ? "" : (":" + in.name));
      items.push_back(std::move(q));
    }
    if (!items.empty()) {
      tuide::AState st;
      st.seeds = opts.seeds;
      st.orphans = fopts.orphans.empty() ? opts.seeds : fopts.orphans;
      const int cap = std::min(tranche_n, static_cast<int>(items.size()));
      items = tuide::a_order_a0_tranche_by_card(root, items, st, cap, nullptr, nullptr);
    }
  } else if (!a_state_path.empty()) {
    std::ifstream in(a_state_path);
    if (!in) {
      std::cerr << "effect-summary-probe: no se pudo leer " << a_state_path << "\n";
      return 2;
    }
    nlohmann::json j;
    in >> j;
    tuide::AState st;
    std::string err;
    if (!tuide::a_state_from_json(j, &st, &err)) {
      std::cerr << "effect-summary-probe: " << err << "\n";
      return 2;
    }
    if (opts.seeds.empty()) {
      opts.seeds = st.seeds;
    }
    const int n = std::min(tranche_n, std::max(0, static_cast<int>(st.queue.size()) - st.cursor));
    for (int i = 0; i < n; ++i) {
      items.push_back(st.queue[static_cast<std::size_t>(st.cursor + i)]);
    }
    if (!items.empty()) {
      items = tuide::a_order_a0_tranche_by_card(root, items, st, static_cast<int>(items.size()),
                                                nullptr, nullptr);
    }
  } else if (!targets.empty()) {
    for (const auto& t : targets) {
      tuide::AQueueItem q;
      q.target = t;
      const auto colon = t.rfind(':');
      if (colon != std::string::npos) {
        q.path = t.substr(0, colon);
        q.symbol = t.substr(colon + 1);
        const auto hash = q.symbol.find('#');
        if (hash != std::string::npos) {
          q.symbol = q.symbol.substr(0, hash);
        }
      } else {
        q.path = t;
      }
      items.push_back(std::move(q));
    }
  } else if (!symbols.empty()) {
    for (const auto& sym : symbols) {
      tuide::AQueueItem q;
      q.symbol = sym;
      q.path = path_hint;
      q.target = path_hint.empty() ? sym : (path_hint + ":" + sym);
      items.push_back(std::move(q));
    }
  } else {
    std::cerr << "effect-summary-probe: falta SYM, --target, --from-map o --from-a-state\n";
    return 2;
  }

  int rc = 0;
  int budget_fail = 0;
  int fallback_n = 0;
  int seed_hit_n = 0;
  int cards_out = 0;
  std::unordered_set<std::string> seen_anchors;
  nlohmann::json report = nlohmann::json::array();

  for (const auto& item : items) {
    const auto es = tuide::effect_summary_for_queue_item(root, item, opts);
    if (!es.anchor.empty() && !seen_anchors.insert(es.anchor).second) {
      continue;
    }
    ++cards_out;
    const auto q = tuide::effect_summary_quality(es, opts.seeds);
    if (!q.within_budget) {
      ++budget_fail;
    }
    if (q.ts_fallback) {
      ++fallback_n;
    }
    if (q.seed_hits > 0) {
      ++seed_hit_n;
    }

    if (json_out) {
      nlohmann::json row = tuide::effect_summary_to_json(es);
      row["target"] = item.target;
      row["quality"] = {{"card_chars", q.card_chars},
                        {"line_count", q.line_count},
                        {"within_budget", q.within_budget},
                        {"ts_fallback", q.ts_fallback},
                        {"seed_hits", q.seed_hits}};
      report.push_back(std::move(row));
    } else {
      std::cout << "======== effect-summary `" << item.target << "` ========\n";
      std::cout << "quality: chars=" << q.card_chars << " lines=" << q.line_count
                << " budget=" << (q.within_budget ? "OK" : "OVER")
                << " fallback=" << (q.ts_fallback ? 1 : 0) << " seed_hits=" << q.seed_hits
                << "\n\n";
      std::cout << es.card_text << "\n";
    }
  }

  if (json_out) {
    std::cout << report.dump(2) << '\n';
  } else {
    std::cout << "---- summary ----\n";
    std::cout << "cards=" << cards_out << " budget_fail=" << budget_fail
              << " ts_fallback=" << fallback_n << " seed_hit_cards=" << seed_hit_n << "\n";
  }
  if (items.empty()) {
    rc = 1;
  }
  return rc;
}

// Bench: Effect Summary card passages vs raw bodies for L1-style embed rerank.
int run_card_embed_bench(const std::string& root, int argc, char** argv) {
  if (!tuide::l2_feat::enabled("L2_EXPLORE_EFFECT_SUMMARY")) {
    std::cerr << "card-embed-bench: L2_EXPLORE_EFFECT_SUMMARY off\n";
    return 2;
  }
  std::string map_path = (fs::path(root) / ".tuide/ai/map_last.md").string();
  std::string query;
  std::string intent;  // short synthesized goal (hybrid)
  std::string tokens_csv;
  std::string modes_csv = "nl,tokens,hybrid,nl+tokens";
  int top_n = 40;
  int body_max_lines = 80;
  // With n_parallel=8 and n_ctx=2048, each slot ≈256 tokens — keep passages short.
  std::size_t body_max_chars = 500;
  std::size_t card_max_chars = 500;
  bool skip_embed = false;
  bool json_out = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--map" && i + 1 < argc) {
      map_path = argv[++i];
    } else if (a == "--query" && i + 1 < argc) {
      query = argv[++i];
    } else if (a == "--intent" && i + 1 < argc) {
      intent = argv[++i];
    } else if (a == "--tokens" && i + 1 < argc) {
      tokens_csv = argv[++i];
    } else if (a == "--modes" && i + 1 < argc) {
      modes_csv = argv[++i];
    } else if (a == "--top" && i + 1 < argc) {
      top_n = std::atoi(argv[++i]);
    } else if (a == "--skip-embed") {
      skip_embed = true;
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr
          << "card-embed-bench [--map map.md] [--query TEXT] [--intent SHORT]\n"
             "  [--tokens a,b,c] [--modes nl,tokens,hybrid,nl+tokens] [--top N]\n"
             "  [--skip-embed] [--json]\n"
             "  nl        = solo prompt crudo\n"
             "  tokens    = solo semantic_tokens / search_terms\n"
             "  hybrid    = intent corto + tokens + síntomas nombrados en el NL\n"
             "  nl+tokens = L1 actual (prompt + cap 12 tokens)\n";
      return 2;
    }
  }
  if (query.empty()) {
    std::cerr << "card-embed-bench: falta --query\n";
    return 2;
  }
  if (intent.empty()) {
    intent = "Locate the relevant code and state transitions";
  }
  std::vector<std::string> semantic_tokens = parse_seeds_csv(tokens_csv);
  std::vector<std::string> modes = parse_seeds_csv(modes_csv);
  if (modes.empty()) {
    modes = {"hybrid"};
  }

  auto lower_copy = [](std::string s) {
    for (char& c : s) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
  };
  // Hybrid rescue: user-overlapping semantic tokens come first.
  auto user_symptom_tokens = [&]() {
    const std::string ql = lower_copy(query);
    std::vector<std::string> out;
    for (const auto& token : semantic_tokens) {
      if (ql.find(lower_copy(token)) != std::string::npos) {
        out.push_back(token);
      }
    }
    return out;
  };

  auto build_mode_query = [&](const std::string& mode) -> std::string {
    if (mode == "nl") {
      return query;
    }
    if (mode == "tokens") {
      std::ostringstream o;
      o << "search:";
      for (const auto& t : semantic_tokens) {
        o << ' ' << t;
      }
      return o.str();
    }
    if (mode == "hybrid") {
      // Intent corto + tokens L1, but user-named symptoms get reserved slots (cap 12).
      std::vector<std::string> rescued = user_symptom_tokens();
      std::vector<std::string> rest;
      auto in_rescued = [&](const std::string& t) {
        const std::string tl = lower_copy(t);
        for (const auto& s : rescued) {
          const std::string sl = lower_copy(s);
          if (tl.find(sl) != std::string::npos || sl.find(tl) != std::string::npos) {
            return true;
          }
        }
        return false;
      };
      for (const auto& t : semantic_tokens) {
        if (!in_rescued(t)) {
          rest.push_back(t);
        }
      }
      std::ostringstream o;
      o << intent << "\nsemantic:";
      int n = 0;
      for (const auto& t : rescued) {
        if (n >= 12) {
          break;
        }
        o << ' ' << t;
        ++n;
      }
      for (const auto& t : rest) {
        if (n >= 12) {
          break;
        }
        o << ' ' << t;
        ++n;
      }
      return o.str();
    }
    // nl+tokens (current L1)
    return tuide::build_semantic_embed_query(query, semantic_tokens, /*max_tokens=*/12);
  };

  const std::string md = read_file(fs::path(map_path));
  if (md.empty()) {
    std::cerr << "card-embed-bench: mapa vacío (" << map_path << ")\n";
    return 2;
  }
  tuide::AQueueMapFilterOpts fopts;
  fopts.want_n = static_cast<std::size_t>(std::max(1, top_n));
  auto inputs = tuide::a_queue_inputs_from_ranked_map_filtered(md, fopts, root);
  if (static_cast<int>(inputs.size()) > top_n) {
    inputs.resize(static_cast<std::size_t>(top_n));
  }
  if (inputs.empty()) {
    std::cerr << "card-embed-bench: 0 entradas desde mapa\n";
    return 2;
  }

  using clock = std::chrono::steady_clock;
  tuide::EffectSummaryOpts es_opts;
  es_opts.seeds = semantic_tokens;
  es_opts.orphans = semantic_tokens;

  struct Row {
    std::string target;
    std::string path;
    std::string symbol;
    int line = 0;
    int map_score = 0;
    std::string card;
    std::string body;
    float card_cos = -1.f;
    float body_cos = -1.f;
    int body_sem_permille = 0;
  };
  std::vector<Row> rows;
  rows.reserve(inputs.size());

  const auto t_card0 = clock::now();
  std::size_t card_chars = 0;
  for (const auto& in : inputs) {
    Row r;
    r.path = in.file;
    r.symbol = in.name;
    r.line = in.line;
    r.map_score = in.score;
    r.body_sem_permille = in.body_sem_permille;
    r.target = in.file + (in.name.empty() ? "" : (":" + in.name));
    tuide::AQueueItem item;
    item.path = in.file;
    item.symbol = in.name;
    item.line = in.line;
    item.stem = in.stem;
    item.score = static_cast<float>(in.score);
    item.target = r.target;
    item.body_sem_permille = in.body_sem_permille;
    item.file_rank = in.file_rank;
    item.file_count = in.file_count;
    item.dup_stem = in.dup_stem;
    item.map_related = in.map_related;
    item.refs_in = in.refs_in;
    es_opts.map_score = in.score;
    es_opts.stem = in.stem;
    es_opts.body_sem_permille = in.body_sem_permille;
    es_opts.file_rank = in.file_rank;
    es_opts.file_count = in.file_count;
    es_opts.dup_stem = in.dup_stem;
    es_opts.map_related = in.map_related;
    es_opts.refs_in = in.refs_in;
    const auto es = tuide::effect_summary_for_queue_item(root, item, es_opts);
    r.card = es.card_text;
    if (r.card.size() > card_max_chars) {
      r.card.resize(card_max_chars);
      r.card += "\n…";
    }
    card_chars += r.card.size();
    rows.push_back(std::move(r));
  }
  const auto card_build_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t_card0).count();

  const auto t_body0 = clock::now();
  std::size_t body_chars = 0;
  int body_ok = 0;
  for (auto& r : rows) {
    GetCodeOfRequest req;
    req.workspace_root = root;
    req.file = r.path;
    req.symbol = r.symbol;
    req.line = r.line;
    req.max_lines = body_max_lines;
    const auto got = get_code_of(req);
    if (got.ok && !got.text.empty()) {
      r.body = got.text;
      if (r.body.size() > body_max_chars) {
        r.body.resize(body_max_chars);
        r.body += "\n…";
      }
      body_chars += r.body.size();
      ++body_ok;
    }
  }
  const auto body_fetch_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t_body0).count();

  int64_t query_embed_ms = 0;
  int64_t card_embed_ms = 0;
  int64_t body_embed_ms = 0;
  bool used_embed = false;
  std::string embed_err;

  struct ModeResult {
    std::string mode;
    std::string enriched;
    std::vector<std::pair<float, std::string>> top_card;
    std::vector<std::pair<float, std::string>> top_body;
  };
  std::vector<ModeResult> mode_results;

  if (!skip_embed) {
    const AiSettings settings = load_ai_settings(root);
    tuide::EmbeddingBackend backend;
    auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
    if (!backend.ensure_ready(settings, progress, &embed_err)) {
      std::cerr << "card-embed-bench: embed ensure_ready failed: " << embed_err
                << " (usa --skip-embed para solo costes de build)\n";
      return 1;
    }

    std::vector<std::string> card_passages;
    std::vector<std::size_t> card_idx;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (!rows[i].card.empty()) {
        card_passages.push_back(rows[i].card);
        card_idx.push_back(i);
      }
    }
    std::vector<std::vector<float>> card_vecs;
    const auto t_c0 = clock::now();
    if (!backend.embed_passages(card_passages, &card_vecs, &embed_err) ||
        card_vecs.size() != card_passages.size()) {
      std::cerr << "card-embed-bench: card embed fail: " << embed_err << "\n";
      return 1;
    }
    card_embed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t_c0).count();

    std::vector<std::string> body_passages;
    std::vector<std::size_t> body_idx;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (!rows[i].body.empty()) {
        body_passages.push_back(rows[i].body);
        body_idx.push_back(i);
      }
    }
    std::vector<std::vector<float>> body_vecs;
    const auto t_b0 = clock::now();
    if (!backend.embed_passages(body_passages, &body_vecs, &embed_err) ||
        body_vecs.size() != body_passages.size()) {
      std::cerr << "card-embed-bench: body embed fail: " << embed_err << "\n";
      return 1;
    }
    body_embed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t_b0).count();

    for (const auto& mode : modes) {
      ModeResult mr;
      mr.mode = mode;
      mr.enriched = build_mode_query(mode);
      std::vector<float> qvec;
      const auto t_q0 = clock::now();
      if (!backend.embed_query(mr.enriched, &qvec, &embed_err) || qvec.empty()) {
        std::cerr << "card-embed-bench: query embed fail (" << mode << "): " << embed_err << "\n";
        return 1;
      }
      query_embed_ms +=
          std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t_q0).count();

      for (auto& r : rows) {
        r.card_cos = -1.f;
        r.body_cos = -1.f;
      }
      for (std::size_t j = 0; j < card_idx.size(); ++j) {
        if (!card_vecs[j].empty()) {
          rows[card_idx[j]].card_cos = tuide::cosine_similarity(qvec, card_vecs[j]);
        }
      }
      for (std::size_t j = 0; j < body_idx.size(); ++j) {
        if (!body_vecs[j].empty()) {
          rows[body_idx[j]].body_cos = tuide::cosine_similarity(qvec, body_vecs[j]);
        }
      }

      auto by_card = rows;
      auto by_body = rows;
      std::stable_sort(by_card.begin(), by_card.end(),
                       [](const Row& a, const Row& b) { return a.card_cos > b.card_cos; });
      std::stable_sort(by_body.begin(), by_body.end(),
                       [](const Row& a, const Row& b) { return a.body_cos > b.body_cos; });

      for (std::size_t i = 0; i < std::min<std::size_t>(5, by_card.size()); ++i) {
        mr.top_card.emplace_back(by_card[i].card_cos, by_card[i].target);
      }
      for (std::size_t i = 0; i < std::min<std::size_t>(5, by_body.size()); ++i) {
        mr.top_body.emplace_back(by_body[i].body_cos, by_body[i].target);
      }
      mode_results.push_back(std::move(mr));
    }
    used_embed = true;
  }

  if (json_out) {
    nlohmann::json j;
    j["n"] = rows.size();
    j["card_build_ms"] = card_build_ms;
    j["body_fetch_ms"] = body_fetch_ms;
    j["card_chars"] = card_chars;
    j["body_chars"] = body_chars;
    j["query_embed_ms"] = query_embed_ms;
    j["card_embed_ms"] = card_embed_ms;
    j["body_embed_ms"] = body_embed_ms;
    j["used_embed"] = used_embed;
    j["intent"] = intent;
    nlohmann::json modes_j = nlohmann::json::array();
    for (const auto& mr : mode_results) {
      modes_j.push_back({{"mode", mr.mode}, {"enriched", mr.enriched}});
    }
    j["modes"] = std::move(modes_j);
    std::cout << j.dump(2) << '\n';
    return 0;
  }

  std::cout << "======== card-embed-bench ========\n";
  std::cout << "map=" << map_path << " n=" << rows.size() << " body_ok=" << body_ok << "\n";
  std::cout << "query_chars=" << query.size() << " intent_chars=" << intent.size()
            << " tokens=" << semantic_tokens.size() << "\n";
  std::cout << "intent=\"" << intent << "\"\n";
  std::cout << "tokens=";
  for (std::size_t i = 0; i < semantic_tokens.size(); ++i) {
    if (i) {
      std::cout << ',';
    }
    std::cout << semantic_tokens[i];
  }
  std::cout << "\n";
  std::cout << "---- build cost ----\n";
  std::cout << "ES cards:  build_ms=" << card_build_ms << " chars_total=" << card_chars
            << " avg=" << (rows.empty() ? 0 : card_chars / rows.size()) << "\n";
  std::cout << "bodies:    fetch_ms=" << body_fetch_ms << " chars_total=" << body_chars
            << " avg=" << (body_ok == 0 ? 0 : body_chars / static_cast<std::size_t>(body_ok))
            << " cap=" << body_max_chars << "\n";
  if (used_embed) {
    std::cout << "---- embed cost ----\n";
    std::cout << "query_embed_ms(sum)=" << query_embed_ms << " card_embed_ms=" << card_embed_ms
              << " body_embed_ms=" << body_embed_ms << "\n";
    for (const auto& mr : mode_results) {
      std::cout << "\n---- mode=" << mr.mode << " enriched ----\n";
      std::cout << mr.enriched.substr(0, 280);
      if (mr.enriched.size() > 280) {
        std::cout << "…";
      }
      std::cout << "\n top5 card:";
      for (std::size_t i = 0; i < mr.top_card.size(); ++i) {
        std::cout << "\n  " << (i + 1) << ". " << mr.top_card[i].first << " `"
                  << mr.top_card[i].second << "`";
      }
      std::cout << "\n";
    }
  } else {
    std::cout << "(embed skipped)\n";
  }
  return 0;
}

int run_a0_sniff_shot(Level2Session& session, const std::string& root, int argc, char** argv) {
  if (!tuide::l2_feat::enabled("L2_EXPLORE_EFFECT_SUMMARY")) {
    std::cerr << "a0-sniff-shot: L2_EXPLORE_EFFECT_SUMMARY off — export "
                 "L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1\n";
    return 2;
  }
  std::string out_dir;
  std::string case_id;
  int max_turns = tuide::kA0MaxTurns;
  bool json_out = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out-dir" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (a == "--case" && i + 1 < argc) {
      case_id = argv[++i];
    } else if (a == "--turns" && i + 1 < argc) {
      max_turns = std::atoi(argv[++i]);
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "a0-sniff-shot [--case CASE_ID] [--turns N] [--out-dir DIR] [--json]\n"
                   "  Sin LLM. Tras bootstrap+A_state: olfateo A0 con veredictos heurísticos.\n"
                   "  Escribe cards/, verdicts_turn*.json, report.json si --out-dir.\n";
      return 2;
    }
  }

  tuide::AState ast = Level2Session::load_a_state(root);
  if (ast.queue.empty()) {
    std::cerr << "a0-sniff-shot: cola vacía — ejecuta bootstrap (+ L1 map) primero\n";
    return 2;
  }
  if (ast.a_subphase.empty()) {
    ast.a_subphase = "a0_sniff";
  }

  std::vector<std::string> expected_stems;
  if (!case_id.empty()) {
    std::string prompt;
    load_case_prompt(root, case_id, &prompt, &expected_stems);
  }

  if (!out_dir.empty()) {
    fs::create_directories(fs::path(out_dir) / "cards");
  }

  tuide::EffectSummaryOpts opts;
  opts.seeds = ast.seeds;

  int turn = 0;
  int total_cards = 0;
  int expands = 0;
  int rejects = 0;
  int uncertain = 0;
  int expand_seed_hits = 0;
  std::vector<std::string> expand_stems;

  while (turn < max_turns && tuide::a_in_a0_sniff(ast) &&
         ast.cursor < static_cast<int>(ast.queue.size()) &&
         ast.cards_used < tuide::kA0MaxCardsTotal) {
    const int n = std::min(tuide::kA0MaxCardsPerTurn,
                           static_cast<int>(ast.queue.size()) - ast.cursor);
    std::vector<tuide::AVerdict> verdicts;
    nlohmann::json turn_json;
    turn_json["turn"] = turn + 1;
    turn_json["cursor"] = ast.cursor;
    nlohmann::json cards = nlohmann::json::array();

    for (int i = 0; i < n; ++i) {
      const auto& item = ast.queue[static_cast<std::size_t>(ast.cursor + i)];
      const auto es = tuide::effect_summary_for_queue_item(root, item, opts);
      const auto q = tuide::effect_summary_quality(es, opts.seeds);
      tuide::AVerdict v = heuristic_a0_verdict(es, item, q);
      verdicts.push_back(v);
      ++total_cards;

      nlohmann::json cj = tuide::effect_summary_to_json(es);
      cj["target"] = item.target;
      cj["heuristic_verdict"] = tuide::a_verdict_kind_name(v.verdict);
      cj["quality"] = {{"seed_hits", q.seed_hits}, {"within_budget", q.within_budget},
                       {"ts_fallback", q.ts_fallback}};
      cards.push_back(std::move(cj));

      if (!out_dir.empty()) {
        const fs::path card_path =
            fs::path(out_dir) / "cards" /
            ("turn" + std::to_string(turn + 1) + "_" + std::to_string(i + 1) + ".md");
        std::ofstream o(card_path);
        o << es.card_text;
      }

      if (v.verdict == tuide::AVerdictKind::Expand) {
        ++expands;
        if (q.seed_hits > 0) {
          ++expand_seed_hits;
        }
        if (!v.stem.empty()) {
          expand_stems.push_back(v.stem);
        }
      } else if (v.verdict == tuide::AVerdictKind::Reject) {
        ++rejects;
      } else if (v.verdict == tuide::AVerdictKind::Uncertain) {
        ++uncertain;
      }
    }

    turn_json["cards"] = cards;
    nlohmann::json vj = nlohmann::json::array();
    for (const auto& v : verdicts) {
      vj.push_back({{"target", v.target},
                    {"verdict", tuide::a_verdict_kind_name(v.verdict)},
                    {"expand_with", tuide::a_expand_modality_name(v.expand_with)},
                    {"why", v.why}});
    }
    turn_json["verdicts"] = vj;

    std::string err;
    if (!tuide::a_apply_a0_verdicts(&ast, verdicts, &err)) {
      std::cerr << "a0-sniff-shot turn " << (turn + 1) << " FAIL: " << err << "\n";
      if (!out_dir.empty()) {
        std::ofstream o(fs::path(out_dir) / ("verdicts_turn" + std::to_string(turn + 1) + ".json"));
        o << turn_json.dump(2) << '\n';
      }
      return 1;
    }
    Level2Session::save_a_state(root, ast, nullptr);

    if (!out_dir.empty()) {
      std::ofstream o(fs::path(out_dir) / ("verdicts_turn" + std::to_string(turn + 1) + ".json"));
      o << turn_json.dump(2) << '\n';
    }

    if (!json_out) {
      std::cout << "---- A0 turn " << (turn + 1) << " cursor=" << ast.cursor << "/"
                << ast.queue.size() << " cards_used=" << ast.cards_used << " ----\n";
      for (const auto& v : verdicts) {
        std::cout << "  [" << tuide::a_verdict_kind_name(v.verdict) << "] `" << v.target << "` — "
                  << v.why << "\n";
      }
      if (ast.a1_active_set) {
        std::cout << "  → A1 queued: " << tuide::a_expand_modality_name(ast.a1_active.modality)
                  << " `" << ast.a1_active.target << "`\n";
      }
    }
    ++turn;
    if (ast.a1_active_set) {
      break;  // stop after first expand batch; A1 is separate probe
    }
  }

  int expected_hits = 0;
  for (const auto& stem : expected_stems) {
    for (const auto& got : expand_stems) {
      if (got == stem || got.find(stem) != std::string::npos) {
        ++expected_hits;
        break;
      }
    }
  }

  nlohmann::json report;
  report["turns"] = turn;
  report["total_cards"] = total_cards;
  report["expands"] = expands;
  report["rejects"] = rejects;
  report["uncertain"] = uncertain;
  report["expand_seed_hit_rate"] =
      expands > 0 ? static_cast<double>(expand_seed_hits) / static_cast<double>(expands) : 0.0;
  report["expected_stems"] = expected_stems;
  report["expected_stem_hits_in_expands"] = expected_hits;
  report["a1_queued"] = ast.a1_active_set;
  report["cards_used"] = ast.cards_used;
  report["cursor"] = ast.cursor;

  if (!out_dir.empty()) {
    std::ofstream o(fs::path(out_dir) / "report.json");
    o << report.dump(2) << '\n';
  }

  if (json_out) {
    std::cout << report.dump(2) << '\n';
  } else {
    std::cout << "---- A0 sniff report ----\n";
    std::cout << "turns=" << turn << " cards=" << total_cards << " expand=" << expands
              << " reject=" << rejects << " uncertain=" << uncertain << "\n";
    std::cout << "expand_seed_hits=" << expand_seed_hits << "/" << expands << "\n";
    if (!expected_stems.empty()) {
      std::cout << "expected_stem_hits=" << expected_hits << "/" << expected_stems.size()
                << "\n";
    }
    if (ast.a1_active_set) {
      std::cout << "next_A1=" << tuide::a_expand_modality_name(ast.a1_active.modality) << " "
                << ast.a1_active.target << "\n";
    }
    std::cout << "PASS_HEURISTIC_A0\n";
  }
  (void)session;
  return 0;
}

struct SummaryProbeCard {
  std::string target;
  std::string body;
};

std::string stem_from_target(const std::string& target) {
  const auto colon = target.rfind(':');
  std::string path = colon != std::string::npos ? target.substr(0, colon) : target;
  const auto slash = path.find_last_of("/\\");
  std::string base = slash != std::string::npos ? path.substr(slash + 1) : path;
  const auto dot = base.rfind('.');
  if (dot != std::string::npos) {
    base = base.substr(0, dot);
  }
  return base;
}

std::vector<SummaryProbeCard> parse_effect_summary_probe_file(const std::string& raw) {
  std::vector<SummaryProbeCard> out;
  const std::string marker = "======== effect-summary `";
  std::size_t pos = 0;
  while ((pos = raw.find(marker, pos)) != std::string::npos) {
    pos += marker.size();
    const auto end_target = raw.find('`', pos);
    if (end_target == std::string::npos) {
      break;
    }
    SummaryProbeCard card;
    card.target = raw.substr(pos, end_target - pos);
    const auto es_start = raw.find("# ES ", end_target);
    if (es_start == std::string::npos) {
      break;
    }
    const auto next_marker = raw.find(marker, es_start + 1);
    const auto summary_line = raw.find("---- summary ----", es_start);
    std::size_t es_end = raw.size();
    if (next_marker != std::string::npos && next_marker < es_end) {
      es_end = next_marker;
    }
    if (summary_line != std::string::npos && summary_line < es_end) {
      es_end = summary_line;
    }
    card.body = raw.substr(es_start, es_end - es_start);
    while (!card.body.empty() && (card.body.back() == '\n' || card.body.back() == '\r')) {
      card.body.pop_back();
    }
    out.push_back(std::move(card));
    pos = es_end;
  }
  return out;
}

std::string format_a0_cards_for_llm(const std::vector<SummaryProbeCard>& cards) {
  std::ostringstream out;
  out << "## Effect Summary (A0 — olfateo; NO cuerpos)\n";
  out << "cards=" << cards.size() << " · juzga TODAS las fichas de la tranche\n";
  out << "Juzga por seeds/nudge/hot/writes/calls; stem/map/kind/path_fam dan contexto L1.\n";
  out << "nudge = sugerencia determinista (expand:*|likely_*|weak_seed), no veredicto.\n";
  out << "Veredictos: expand|reject|uncertain (PROHIBIDO useful).\n\n";
  for (std::size_t i = 0; i < cards.size(); ++i) {
    out << "### card " << (i + 1) << " `" << cards[i].target << "`\n\n```\n" << cards[i].body
        << "\n```\n\n";
  }
  out << "Responde {\"action\":\"a_judge\",\"phase\":\"a0_sniff\",\"verdicts\":[…],"
         "\"done\":false}\n";
  return out.str();
}

int run_a0_sniff_judge_shot(Level2Session& session, const std::string& root, int argc,
                            char** argv) {
  if (!tuide::l2_feat::enabled("L2_EXPLORE_EFFECT_SUMMARY")) {
    std::cerr << "a0-sniff-judge-shot: L2_EXPLORE_EFFECT_SUMMARY off — export "
                 "L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1\n";
    return 2;
  }
  std::string summary_path;
  std::string out_dir;
  std::string case_id;
  bool dry = false;
  bool json_out = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--from-summary" && i + 1 < argc) {
      summary_path = argv[++i];
    } else if (a == "--out-dir" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (a == "--case" && i + 1 < argc) {
      case_id = argv[++i];
    } else if (a == "--dry") {
      dry = true;
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "a0-sniff-judge-shot [--case CASE_ID] --from-summary FILE [--out-dir DIR]\n"
                   "  [--dry] [--json]\n"
                   "  1× LLM: fichas Effect Summary → a_judge phase=a0_sniff.\n"
                   "  Sin --from-summary usa build_a_peek_tranche_markdown (requiere a_state).\n";
      return 2;
    }
  }
  if (case_id.empty()) {
    std::cerr << "a0-sniff-judge-shot: requiere --case CASE_ID\n";
    return 2;
  }

  std::string instruction;
  std::vector<std::string> expected_stems;
  std::vector<std::string> trap_stems;
  if (!load_case_prompt(root, case_id, &instruction, &expected_stems, &trap_stems)) {
    std::cerr << "a0-sniff-judge-shot: case `" << case_id << "` no encontrado\n";
    return 2;
  }

  std::string cards_md;
  std::vector<SummaryProbeCard> cards;
  if (!summary_path.empty()) {
    const std::string raw = read_file(summary_path);
    if (raw.empty()) {
      std::cerr << "a0-sniff-judge-shot: no se pudo leer " << summary_path << "\n";
      return 2;
    }
    cards = parse_effect_summary_probe_file(raw);
    if (cards.empty()) {
      std::cerr << "a0-sniff-judge-shot: sin fichas en " << summary_path << "\n";
      return 2;
    }
    cards_md = format_a0_cards_for_llm(cards);
  } else {
    tuide::AState ast = Level2Session::load_a_state(root);
    if (ast.queue.empty()) {
      std::cerr << "a0-sniff-judge-shot: cola vacía — bootstrap o usa --from-summary\n";
      return 2;
    }
    if (ast.a_subphase.empty()) {
      ast.a_subphase = "a0_sniff";
    }
    cards_md = session.build_a_peek_tranche_markdown(root);
    for (const auto& item : ast.queue) {
      SummaryProbeCard c;
      c.target = item.target;
      cards.push_back(std::move(c));
    }
  }

  std::ostringstream user;
  user << "phase=explore_a step=1 workflow=agent subphase=a0_sniff\n\n";
  user << "## Instruction\n" << instruction << "\n\n";
  user << cards_md;

  const std::string system =
      "Eres el Nivel 2 en fase explore_a — subfase A0 (Effect Summary / olfateo).\n"
      "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
      "PROHIBIDO action=plan, tool, edit, done next=edit, useful en A0.\n"
      "\n"
      "## Fichas → a_judge phase=a0_sniff\n"
      "Juzga por seeds/nudge/hot/writes/calls; stem/map dan contexto L1.\n"
      "nudge = sugerencia determinista (expand:*|likely_glue|likely_noise|weak_seed), no veredicto.\n"
      "expand = merece peek|trail|dataflow (expand_with). reject = fuera. uncertain = duda.\n"
      "Traps: cancel genérico, LSP completion, UI glue sin seeds → reject.\n"
      "Juzga solo por seeds/nudge/hot/writes de cada ficha; likely_* → reject salvo seeds "
      "fuertes.\n"
      "expand_with según nudge (NO dataflow en A0 salvo nudge expand:dataflow).\n"
      "Ejemplo genérico:\n"
      "{\"action\":\"a_judge\",\"phase\":\"a0_sniff\",\"verdicts\":["
      "{\"target\":\"src/foo/module.cpp:sym_a\",\"verdict\":\"expand\","
      "\"expand_with\":\"trail\",\"why\":\"nudge expand:trail + seeds\"},"
      "{\"target\":\"src/lsp/lsp_client.cpp:cancel\",\"verdict\":\"reject\","
      "\"why\":\"likely_lsp_trap\"}],\"done\":false}\n";

  std::cout << "======== a0-sniff-judge-shot ========\n";
  std::cout << "case=" << case_id << " cards=" << cards.size() << "\n";
  std::cout << "expected_stems=";
  for (std::size_t i = 0; i < expected_stems.size(); ++i) {
    if (i) {
      std::cout << ",";
    }
    std::cout << expected_stems[i];
  }
  std::cout << " trap_stems=";
  for (std::size_t i = 0; i < trap_stems.size(); ++i) {
    if (i) {
      std::cout << ",";
    }
    std::cout << trap_stems[i];
  }
  std::cout << "\nprompt_chars system=" << system.size() << " user=" << user.str().size() << "\n";

  if (!out_dir.empty()) {
    fs::create_directories(out_dir);
    std::ofstream(fs::path(out_dir) / "system.txt") << system;
    std::ofstream(fs::path(out_dir) / "user.md") << user.str();
    std::ofstream(fs::path(out_dir) / "cards.md") << cards_md;
  }

  if (dry) {
    std::cout << "\n--- user.md (dry) ---\n" << user.str() << "\n";
    std::cout << "dry: no LLM\n";
    return 0;
  }

  const AiSettings settings = load_ai_settings(root);
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  auto brain_ptr = ready_l2_brain(settings, progress, &err);
  if (!brain_ptr) {
    std::cerr << "a0-sniff-judge-shot: " << err << "\n";
    return err.find("mode") != std::string::npos ? 2 : 1;
  }
  tuide::L2Brain& brain = *brain_ptr;

  tuide::L2BrainRequest breq;
  breq.system_prompt = system;
  breq.user_prompt = user.str();
  breq.phase = "explore_a";
  breq.max_tokens = settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 1024;
  breq.n_ctx = settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192;
  breq.temperature = 0.1f;

  std::cout << "L2 ▸ a0-sniff-judge-shot pidiendo a_judge (" << brain.name() << ")…\n";
  const auto bres = tuide::propose_with_think(brain, &breq, nullptr);
  if (!bres.ok) {
    std::cerr << "LLM FAIL: " << bres.error << "\n";
    return 1;
  }
  if (!out_dir.empty()) {
    std::ofstream(fs::path(out_dir) / "model_raw.txt") << bres.text;
  }
  std::cout << "\n--- model ---\n" << bres.text << "\n---\n";

  const tuide::L2Action action = tuide::parse_l2_action(bres.text);
  if (action.kind != tuide::L2ActionKind::AJudge && action.name != "a_judge") {
    std::cout << "WARN: action kind=" << static_cast<int>(action.kind) << " name=" << action.name
              << " error=" << action.error << "\n";
    return 1;
  }

  auto normalize_verdict = [](const std::string& raw) -> std::string {
    if (raw == "expand") {
      return "expand";
    }
    if (raw.rfind("expand:", 0) == 0) {
      return "expand";
    }
    if (raw == "reject" || raw == "likely_lsp_trap" || raw == "likely_noise" ||
        raw == "likely_glue") {
      return "reject";
    }
    if (raw == "uncertain" || raw == "no_signal" || raw == "weak_seed") {
      return "uncertain";
    }
    return raw.empty() ? "unknown" : raw;
  };

  struct RawVerdict {
    std::string target;
    std::string verdict_raw;
    std::string expand_with;
    std::string why;
  };
  std::vector<RawVerdict> raw_verdicts;
  try {
    const auto j = nlohmann::json::parse(bres.text);
    if (j.contains("verdicts") && j["verdicts"].is_array()) {
      for (const auto& v : j["verdicts"]) {
        RawVerdict rv;
        rv.target = v.value("target", "");
        rv.verdict_raw = v.value("verdict", "");
        rv.expand_with = v.value("expand_with", "");
        rv.why = v.value("why", "");
        raw_verdicts.push_back(std::move(rv));
      }
    }
  } catch (...) {
  }
  if (raw_verdicts.empty()) {
    for (const auto& v : action.a_verdicts) {
      RawVerdict rv;
      rv.target = v.target;
      rv.verdict_raw = tuide::a_verdict_kind_name(v.verdict);
      rv.expand_with = tuide::a_expand_modality_name(v.expand_with);
      rv.why = v.why;
      raw_verdicts.push_back(std::move(rv));
    }
  }

  auto stem_is = [](const std::string& stem, const std::vector<std::string>& needles) {
    for (const auto& n : needles) {
      if (stem == n) {
        return true;
      }
    }
    return false;
  };

  int expands = 0;
  int rejects = 0;
  int uncertain = 0;
  int expected_expands = 0;
  int trap_expands = 0;
  nlohmann::json rows = nlohmann::json::array();

  std::cout << "\n---- verdicts (normalizados) ----\n";
  for (const auto& v : raw_verdicts) {
    const std::string stem = stem_from_target(v.target);
    const std::string norm = normalize_verdict(v.verdict_raw);
    std::cout << v.target << " → " << norm;
    if (norm == "expand") {
      if (!v.expand_with.empty()) {
        std::cout << " (" << v.expand_with << ")";
      } else if (v.verdict_raw.rfind("expand:", 0) == 0) {
        std::cout << " (" << v.verdict_raw.substr(7) << ")";
      }
    }
    if (!v.verdict_raw.empty() && v.verdict_raw != norm) {
      std::cout << " [raw=" << v.verdict_raw << "]";
    }
    if (!v.why.empty()) {
      std::cout << " — " << v.why;
    }
    std::cout << " [stem=" << stem << "]\n";

    if (norm == "expand") {
      ++expands;
      if (stem_is(stem, expected_stems)) {
        ++expected_expands;
      }
      if (stem_is(stem, trap_stems)) {
        ++trap_expands;
      }
    } else if (norm == "reject") {
      ++rejects;
    } else if (norm == "uncertain") {
      ++uncertain;
    }

    rows.push_back({{"target", v.target},
                    {"stem", stem},
                    {"verdict", norm},
                    {"verdict_raw", v.verdict_raw},
                    {"expand_with", v.expand_with},
                    {"why", v.why},
                    {"expected_stem", stem_is(stem, expected_stems)},
                    {"trap_stem", stem_is(stem, trap_stems)}});
  }

  nlohmann::json report;
  report["case_id"] = case_id;
  report["cards"] = static_cast<int>(cards.size());
  report["verdicts"] = static_cast<int>(raw_verdicts.size());
  report["expand"] = expands;
  report["reject"] = rejects;
  report["uncertain"] = uncertain;
  report["expected_stem_expands"] = expected_expands;
  report["trap_stem_expands"] = trap_expands;
  report["expected_stems"] = expected_stems;
  report["trap_stems"] = trap_stems;
  report["rows"] = rows;

  if (!out_dir.empty()) {
    std::ofstream(fs::path(out_dir) / "verdicts.json") << report.dump(2) << '\n';
  }

  if (json_out) {
    std::cout << report.dump(2) << '\n';
  } else {
    std::cout << "\n---- summary ----\n";
    std::cout << "expand=" << expands << " reject=" << rejects << " uncertain=" << uncertain
              << " verdicts=" << raw_verdicts.size() << "/" << cards.size() << "\n";
    std::cout << "expected_stem_expands=" << expected_expands << "/" << expected_stems.size()
              << " trap_expands=" << trap_expands << "\n";
  }
  return 0;
}

std::string normalize_a0_verdict_raw(const std::string& raw) {
  if (raw == "expand") {
    return "expand";
  }
  if (raw.rfind("expand:", 0) == 0) {
    return "expand";
  }
  if (raw == "reject" || raw == "likely_lsp_trap" || raw == "likely_noise" ||
      raw == "likely_glue") {
    return "reject";
  }
  if (raw == "uncertain" || raw == "no_signal" || raw == "weak_seed") {
    return "uncertain";
  }
  return raw.empty() ? "unknown" : raw;
}

bool target_in_tranche(const std::vector<tuide::AQueueItem>& tranche, const std::string& target) {
  for (const auto& item : tranche) {
    if (tuide::a_target_matches_verdict_anchor(item.target, target)) {
      return true;
    }
  }
  return false;
}

int run_a0_tranche_rank_shot(Level2Session& /*session*/, const std::string& root, int argc,
                             char** argv) {
  if (!tuide::l2_feat::enabled("L2_EXPLORE_EFFECT_SUMMARY")) {
    std::cerr << "a0-tranche-rank-shot: L2_EXPLORE_EFFECT_SUMMARY off\n";
    return 2;
  }
  std::string out_dir;
  std::string case_id;
  int max_cards = 12;
  bool json_out = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out-dir" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (a == "--case" && i + 1 < argc) {
      case_id = argv[++i];
    } else if (a == "--max-cards" && i + 1 < argc) {
      max_cards = std::atoi(argv[++i]);
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "a0-tranche-rank-shot [--case CASE_ID] [--max-cards N] [--out-dir DIR] [--json]\n"
                   "  Sin LLM. Compara slice map-order vs rerank por ficha Effect Summary.\n";
      return 2;
    }
  }
  if (case_id.empty()) {
    std::cerr << "a0-tranche-rank-shot: requiere --case CASE_ID\n";
    return 2;
  }

  std::string query;
  if (!load_case_prompt(root, case_id, &query, nullptr, nullptr)) {
    std::cerr << "a0-tranche-rank-shot: case `" << case_id << "` no encontrado\n";
    return 2;
  }

  tuide::AState ast = Level2Session::load_a_state(root);
  if (ast.queue.empty()) {
    std::cerr << "a0-tranche-rank-shot: cola vacía — bootstrap primero\n";
    return 2;
  }

  const int n = std::min(max_cards, std::max(0, static_cast<int>(ast.queue.size()) - ast.cursor));
  if (n <= 0) {
    std::cerr << "a0-tranche-rank-shot: slice vacío cursor=" << ast.cursor << "\n";
    return 2;
  }
  std::vector<tuide::AQueueItem> slice;
  slice.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    slice.push_back(ast.queue[static_cast<std::size_t>(ast.cursor + i)]);
  }

  std::vector<tuide::A0CardRankRow> card_rows;
  const auto meta_order = tuide::a_order_a0_tranche(slice, ast, n);
  const auto card_order =
      tuide::a_order_a0_tranche_by_card(root, slice, ast, n, nullptr, &card_rows);

  nlohmann::json report;
  report["case_id"] = case_id;
  report["max_cards"] = n;
  report["seeds_n"] = ast.seeds.size();
  report["orphans_n"] = ast.orphans.size();
  nlohmann::json rows = nlohmann::json::array();
  std::unordered_map<std::string, int> card_rank;
  for (std::size_t i = 0; i < card_order.size(); ++i) {
    card_rank[card_order[i].target] = static_cast<int>(i) + 1;
  }

  std::cout << "======== a0-tranche-rank-shot ========\n";
  std::cout << "case=" << case_id << " slice_n=" << n << " seeds=" << ast.seeds.size()
            << " orphans=" << ast.orphans.size() << "\n\n";
  std::cout << "---- map/metadata order (a_order_a0_tranche) ----\n";
  for (std::size_t i = 0; i < meta_order.size(); ++i) {
    std::cout << (i + 1) << ". `" << meta_order[i].target << "` stem=" << meta_order[i].stem
              << "\n";
  }
  std::cout << "\n---- card-body rerank (effect_summary_lexical_rerank_score) ----\n";
  std::cout << "rank  score     was  nudge                target\n";
  for (std::size_t i = 0; i < card_rows.size() && static_cast<int>(i) < n; ++i) {
    const auto& row = card_rows[i];
    const int new_rank = static_cast<int>(i) + 1;
    const int was = row.slice_rank;
    const int delta = was - new_rank;
    std::cout << new_rank << "  " << row.score << "  " << was << "  "
              << row.es.nudge.substr(0, 20) << (row.es.nudge.size() > 20 ? "…" : "") << "  `"
              << row.item.target << "`";
    if (delta != 0) {
      std::cout << " (" << (delta > 0 ? "+" : "") << delta << ")";
    }
    if (!row.es.seed_match.empty()) {
      std::cout << " seeds=" << row.es.seed_match.size();
    }
    if (!row.es.orphan_match.empty()) {
      std::cout << " orphan=" << row.es.orphan_match.front();
    }
    if (std::find(row.es.hot.begin(), row.es.hot.end(), std::string("spinner")) !=
        row.es.hot.end()) {
      std::cout << " hot=spinner";
    }
    std::cout << "\n";

    rows.push_back({{"rank", new_rank},
                    {"score", row.score},
                    {"slice_rank", was},
                    {"delta", delta},
                    {"target", row.item.target},
                    {"stem", row.item.stem},
                    {"nudge", row.es.nudge},
                    {"seed_match", row.es.seed_match},
                    {"orphan_match", row.es.orphan_match},
                    {"hot", row.es.hot},
                    {"in_tranche", card_rank.count(row.item.target) > 0}});
  }
  report["rows"] = rows;
  report["meta_order"] = nlohmann::json::array();
  for (const auto& item : meta_order) {
    report["meta_order"].push_back(item.target);
  }
  report["card_order"] = nlohmann::json::array();
  for (const auto& item : card_order) {
    report["card_order"].push_back(item.target);
  }

  if (!out_dir.empty()) {
    fs::create_directories(out_dir);
    std::ofstream(fs::path(out_dir) / "rank_report.json") << report.dump(2) << '\n';
  }
  if (json_out) {
    std::cout << report.dump(2) << '\n';
  }
  return 0;
}

int run_a0_first_judge_shot(Level2Session& session, const std::string& root, int argc,
                            char** argv) {
  if (!tuide::l2_feat::enabled("L2_EXPLORE_EFFECT_SUMMARY")) {
    std::cerr << "a0-first-judge-shot: L2_EXPLORE_EFFECT_SUMMARY off — export "
                 "L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1\n";
    return 2;
  }
  std::string out_dir;
  std::string case_id;
  int max_cards = 8;
  bool dry = false;
  bool json_out = false;
  bool require_full = true;
  bool try_apply = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out-dir" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (a == "--case" && i + 1 < argc) {
      case_id = argv[++i];
    } else if (a == "--max-cards" && i + 1 < argc) {
      max_cards = std::atoi(argv[++i]);
    } else if (a == "--dry") {
      dry = true;
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "--apply") {
      try_apply = true;
    } else if (a == "--no-require-full") {
      require_full = false;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "a0-first-judge-shot [--case CASE_ID] [--max-cards N] [--out-dir DIR]\n"
                   "  [--dry] [--json] [--apply] [--no-require-full]\n"
                   "  Solo 1er turno A0: tranche Effect Summary → a_judge (sin A1/a_done).\n"
                   "  Valida un veredicto por card mostrada. Default max-cards=8.\n";
      return 2;
    }
  }
  if (case_id.empty()) {
    std::cerr << "a0-first-judge-shot: requiere --case CASE_ID\n";
    return 2;
  }

  std::string instruction;
  std::vector<std::string> expected_stems;
  std::vector<std::string> trap_stems;
  if (!load_case_prompt(root, case_id, &instruction, &expected_stems, &trap_stems)) {
    std::cerr << "a0-first-judge-shot: case `" << case_id << "` no encontrado\n";
    return 2;
  }

  tuide::AState ast = Level2Session::load_a_state(root);
  if (ast.queue.empty()) {
    std::cerr << "a0-first-judge-shot: cola vacía — ejecuta bootstrap (+ L1 map) primero\n";
    return 2;
  }
  if (ast.a_subphase.empty()) {
    ast.a_subphase = "a0_sniff";
  }

  const tuide::A0TrancheShown shown =
      tuide::a_build_a0_tranche_shown(root, ast, max_cards, nullptr);
  if (shown.items.empty()) {
    std::cerr << "a0-first-judge-shot: tranche vacía (cursor=" << ast.cursor << "/"
              << ast.queue.size() << ")\n";
    return 2;
  }

  const std::string cards_md = session.build_a_peek_tranche_markdown(root, max_cards);

  nlohmann::json tranche_json = nlohmann::json::array();
  bool has_expected_stem = false;
  for (std::size_t i = 0; i < shown.items.size(); ++i) {
    const auto& item = shown.items[i];
    if (std::find(expected_stems.begin(), expected_stems.end(), item.stem) !=
        expected_stems.end()) {
      has_expected_stem = true;
    }
    tranche_json.push_back({{"i", i + 1},
                            {"target", item.target},
                            {"path", item.path},
                            {"symbol", item.symbol},
                            {"stem", item.stem},
                            {"score", item.score}});
  }

  std::ostringstream user;
  user << "phase=explore_a step=1 workflow=agent subphase=a0_sniff\n\n";
  user << "## Instruction\n" << instruction << "\n\n";
  user << cards_md;

  const std::string system =
      "Eres el Nivel 2 en fase explore_a — subfase A0 (Effect Summary / olfateo).\n"
      "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
      "PROHIBIDO action=plan, tool, edit, done next=edit, useful|interesting en A0.\n"
      "\n"
      "## Fichas → a_judge phase=a0_sniff\n"
      "Debes emitir EXACTAMENTE " +
      std::to_string(shown.items.size()) +
      " veredictos: uno por cada ### card (mismo target).\n"
      "Veredictos: expand|reject|uncertain. expand requiere expand_with (peek|trail|dataflow).\n"
      "Traps: cancel genérico, LSP completion, UI glue sin seeds → reject.\n"
      "Juzga solo por seeds/nudge/hot/writes de cada ficha; likely_* → reject salvo seeds "
      "fuertes.\n"
      "expand_with según nudge (NO dataflow en A0 salvo nudge expand:dataflow).\n"
      "Ejemplo con 2 cards (targets ficticios):\n"
      "{\"action\":\"a_judge\",\"phase\":\"a0_sniff\",\"verdicts\":["
      "{\"target\":\"src/foo/module.cpp:sym_a#tail\",\"verdict\":\"expand\","
      "\"expand_with\":\"trail\",\"why\":\"nudge expand:trail + seeds\"},"
      "{\"target\":\"src/lsp/lsp_client.cpp:cancel#tail\",\"verdict\":\"reject\","
      "\"why\":\"likely_lsp_trap\"}],\"done\":false}\n";

  std::cout << "======== a0-first-judge-shot ========\n";
  std::cout << "case=" << case_id << " max_cards=" << max_cards << " slice_n=" << shown.slice_n
            << " shown=" << shown.items.size() << " char_trunc=" << (shown.char_truncated ? 1 : 0)
            << " expected_stem_in_tranche=" << (has_expected_stem ? 1 : 0) << "\n";
  std::cout << "prompt_chars system=" << system.size() << " user=" << user.str().size() << "\n";

  if (!out_dir.empty()) {
    fs::create_directories(out_dir);
    std::ofstream(fs::path(out_dir) / "system.txt") << system;
    std::ofstream(fs::path(out_dir) / "user.md") << user.str();
    std::ofstream(fs::path(out_dir) / "cards.md") << cards_md;
    std::ofstream(fs::path(out_dir) / "tranche.json") << tranche_json.dump(2) << '\n';
    tuide::EffectSummaryOpts es_opts;
    es_opts.seeds = ast.seeds;
    es_opts.orphans = ast.orphans;
    if (es_opts.orphans.empty()) {
      es_opts.orphans = ast.seeds;
    }
    for (std::size_t i = 0; i < shown.items.size(); ++i) {
      const auto& item = shown.items[i];
      es_opts.map_score = static_cast<int>(item.score);
      es_opts.stem = item.stem;
      es_opts.map_related = item.map_related;
      es_opts.refs_in = item.refs_in;
      es_opts.body_sem_permille = item.body_sem_permille;
      es_opts.file_rank = item.file_rank;
      es_opts.file_count = item.file_count;
      es_opts.dup_stem = item.dup_stem;
      const auto es = tuide::effect_summary_for_queue_item(root, item, es_opts);
      const fs::path card_path =
          fs::path(out_dir) / "cards" / ("card_" + std::to_string(i + 1) + ".md");
      fs::create_directories(card_path.parent_path());
      std::ofstream(card_path) << es.card_text;
    }
  }

  if (dry) {
    std::cout << "\n--- tranche targets ---\n";
    for (std::size_t i = 0; i < shown.items.size(); ++i) {
      std::cout << (i + 1) << ". `" << shown.items[i].target << "` stem=" << shown.items[i].stem
                << "\n";
    }
    std::cout << "dry: no LLM\n";
    return 0;
  }

  const AiSettings settings = load_ai_settings(root);
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  auto brain_ptr = ready_l2_brain(settings, progress, &err);
  if (!brain_ptr) {
    std::cerr << "a0-first-judge-shot: " << err << "\n";
    return err.find("mode") != std::string::npos ? 2 : 1;
  }
  tuide::L2Brain& brain = *brain_ptr;

  tuide::L2BrainRequest breq;
  breq.system_prompt = system;
  breq.user_prompt = user.str();
  breq.phase = "explore_a";
  breq.max_tokens = settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 2048;
  breq.n_ctx = settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192;
  breq.temperature = 0.1f;

  std::cout << "L2 ▸ a0-first-judge-shot pidiendo a_judge (" << brain.name() << ")…\n";
  const auto bres = tuide::propose_with_think(brain, &breq, nullptr);
  if (!bres.ok) {
    std::cerr << "LLM FAIL: " << bres.error << "\n";
    return 1;
  }
  if (!out_dir.empty()) {
    std::ofstream(fs::path(out_dir) / "model_raw.txt") << bres.text;
  }
  std::cout << "\n--- model ---\n" << bres.text << "\n---\n";

  const tuide::L2Action action = tuide::parse_l2_action(bres.text);
  std::vector<tuide::AVerdict> parsed_verdicts = action.a_verdicts;
  if (parsed_verdicts.empty()) {
    try {
      const auto j = nlohmann::json::parse(bres.text);
      tuide::parse_a_verdicts_array(j, &parsed_verdicts, &err);
    } catch (...) {
    }
  }

  int covered = 0;
  nlohmann::json missing = nlohmann::json::array();
  nlohmann::json rows = nlohmann::json::array();
  for (const auto& item : shown.items) {
    bool hit = false;
    for (const auto& v : parsed_verdicts) {
      if (tuide::a_target_matches_verdict_anchor(item.target, v.target)) {
        hit = true;
        ++covered;
        rows.push_back({{"target", item.target},
                        {"verdict", tuide::a_verdict_kind_name(v.verdict)},
                        {"expand_with", tuide::a_expand_modality_name(v.expand_with)},
                        {"why", v.why}});
        break;
      }
    }
    if (!hit) {
      missing.push_back(item.target);
    }
  }

  nlohmann::json extra = nlohmann::json::array();
  for (const auto& v : parsed_verdicts) {
    if (!target_in_tranche(shown.items, v.target)) {
      extra.push_back({{"target", v.target},
                       {"verdict", tuide::a_verdict_kind_name(v.verdict)},
                       {"why", v.why}});
    }
  }

  nlohmann::json coverage;
  coverage["shown"] = shown.items.size();
  coverage["verdicts"] = parsed_verdicts.size();
  coverage["covered"] = covered;
  coverage["missing"] = missing;
  coverage["extra"] = extra;
  coverage["full"] = (covered >= static_cast<int>(shown.items.size()));
  coverage["expected_stem_in_tranche"] = has_expected_stem;

  bool apply_ok = false;
  std::string apply_err;
  if (try_apply && !parsed_verdicts.empty()) {
    tuide::AState apply_st = ast;
    apply_st.a0_shown_targets.clear();
    for (const auto& item : shown.items) {
      apply_st.a0_shown_targets.push_back(item.target);
    }
    apply_ok = tuide::a_apply_a0_verdicts(&apply_st, parsed_verdicts, &apply_err, &root);
    coverage["apply_ok"] = apply_ok;
    coverage["apply_err"] = apply_err;
  }

  if (!out_dir.empty()) {
    std::ofstream(fs::path(out_dir) / "coverage.json") << coverage.dump(2) << '\n';
    std::ofstream(fs::path(out_dir) / "verdicts_parsed.json") << rows.dump(2) << '\n';
  }

  if (json_out) {
    std::cout << coverage.dump(2) << '\n';
  } else {
    std::cout << "\n---- coverage ----\n";
    std::cout << "covered=" << covered << "/" << shown.items.size()
              << " verdicts=" << parsed_verdicts.size() << " extra=" << extra.size() << "\n";
    if (!missing.empty()) {
      std::cout << "missing:\n";
      for (const auto& m : missing) {
        std::cout << "  - " << m.get<std::string>() << "\n";
      }
    }
    if (!extra.empty()) {
      std::cout << "extra (no en tranche):\n";
      for (const auto& e : extra) {
        std::cout << "  - " << e.value("target", "") << "\n";
      }
    }
    if (try_apply) {
      std::cout << "apply_a0=" << (apply_ok ? "OK" : "FAIL");
      if (!apply_err.empty()) {
        std::cout << " — " << apply_err;
      }
      std::cout << "\n";
    }
  }

  if (require_full && covered < static_cast<int>(shown.items.size())) {
    return 1;
  }
  if (try_apply && !apply_ok) {
    return 1;
  }
  return 0;
}

std::string cerca_ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::vector<float> cerca_blob_vec(const void* p, int nbytes) {
  std::vector<float> v;
  if (p == nullptr || nbytes < static_cast<int>(sizeof(float))) {
    return v;
  }
  const std::size_t n = static_cast<std::size_t>(nbytes) / sizeof(float);
  v.resize(n);
  std::memcpy(v.data(), p, n * sizeof(float));
  return v;
}

void wave_split_hop_loc(const std::string& loc, std::string* path, std::string* symbol) {
  if (path == nullptr || symbol == nullptr) {
    return;
  }
  const auto slash = loc.find_last_of("/\\");
  const auto colon = loc.rfind(':');
  if (colon != std::string::npos && (slash == std::string::npos || colon > slash)) {
    *path = loc.substr(0, colon);
    *symbol = loc.substr(colon + 1);
    return;
  }
  const auto col2 = loc.rfind("::");
  if (col2 != std::string::npos && col2 + 2 < loc.size()) {
    *symbol = loc.substr(col2 + 2);
    *path = {};
    return;
  }
  *path = {};
  *symbol = loc;
}

void wave_rank_peek_hops(tuide::EffectRegistry* r, tuide::EmbeddingBackend* embed, const std::string& root,
                         const std::string& query, std::vector<tuide::WavePeekHop>* hops) {
  if (r == nullptr || r->db == nullptr || hops == nullptr || hops->empty() || query.empty()) {
    return;
  }
  if (embed == nullptr || !embed->ready()) {
    return;
  }
  std::vector<float> qvec;
  std::string err;
  if (!embed->embed_query(query, &qvec, &err) || qvec.empty()) {
    return;
  }
  const std::string model_key = tuide::registry_embed_model_key(
      tuide::kRegistryEmbedModelDefault, tuide::RegistryMatchSurface::CardFull);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(r->db,
                         "SELECT e.blob FROM embeddings e JOIN nodes n ON n.id=e.node_id "
                         "WHERE e.model=?1 AND n.tombstone_reason='' AND "
                         "((n.path=?2 AND n.symbol=?3) OR n.id=?4 OR n.id=?5) LIMIT 1",
                         -1, &st, nullptr) != SQLITE_OK) {
    return;
  }
  for (auto& h : *hops) {
    std::string path;
    std::string symbol;
    wave_split_hop_loc(h.loc, &path, &symbol);
    if (symbol.empty()) {
      continue;
    }
    const std::string id = tuide::registry_canonical_fn_id(root, path, symbol);
    const std::string alt = path.empty() ? symbol : (path + ":" + symbol);
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    sqlite3_bind_text(st, 1, model_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, alt.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
      const std::vector<float> v =
          cerca_blob_vec(sqlite3_column_blob(st, 0), sqlite3_column_bytes(st, 0));
      if (!v.empty()) {
        h.cosine = tuide::cosine_similarity(qvec, v);
      }
    }
  }
  sqlite3_finalize(st);
}

bool cerca_scope_match_one(const tuide::RegistryNodeRow& n, const std::string& sc) {
  const std::string want = cerca_ascii_lower(sc);
  if (want.empty()) {
    return false;
  }
  const std::string stem = cerca_ascii_lower(n.stem);
  const std::string path = cerca_ascii_lower(n.path);
  const std::string id = cerca_ascii_lower(n.id);
  const std::string sym = cerca_ascii_lower(n.symbol);
  if (!stem.empty() && stem == want) {
    return true;
  }
  if (!path.empty() && path.find(want) != std::string::npos) {
    return true;
  }
  if (!sym.empty() && (sym == want || id.find(want) != std::string::npos)) {
    return true;
  }
  return false;
}

bool cerca_scope_match(const tuide::RegistryNodeRow& n, const std::vector<std::string>& in_scopes) {
  if (in_scopes.empty()) {
    return true;
  }
  for (const auto& sc : in_scopes) {
    if (cerca_scope_match_one(n, sc)) {
      return true;
    }
  }
  return false;
}

std::string cerca_clip_card(std::string p) {
  for (char& c : p) {
    if (c == '\n' || c == '\r') {
      c = ' ';
    }
  }
  while (!p.empty() && p.back() == ' ') {
    p.pop_back();
  }
  if (p.size() > 96) {
    p.resize(96);
    p += "…";
  }
  return p;
}

void cerca_bfs_ids(tuide::EffectRegistry* r,
                   const std::vector<std::pair<std::string, std::string>>& seed_fns, int hops,
                   std::unordered_map<std::string, int>* dist) {
  if (r == nullptr || dist == nullptr) {
    return;
  }
  std::queue<std::string> q;
  auto push0 = [&](const std::string& id) {
    if (id.empty() || dist->count(id) != 0) {
      return;
    }
    tuide::RegistryNodeRow row;
    std::string err;
    if (!tuide::registry_get(r, id, &row, &err) || !row.tombstone_reason.empty()) {
      return;
    }
    (*dist)[id] = 0;
    q.push(id);
  };
  for (const auto& s : seed_fns) {
    if (s.second.empty()) {
      continue;
    }
    std::string id = tuide::registry_canonical_fn_id(r->workspace_root, s.first, s.second);
    push0(id);
    if (dist->count(id) == 0 && !s.second.empty()) {
      // Tail lookup via neighbors is skipped; try id as the symbol alone.
      push0(s.second);
    }
  }
  const int max_hops = std::max(0, hops);
  const int cap = 400;
  while (!q.empty() && static_cast<int>(dist->size()) < cap) {
    const std::string cur = q.front();
    q.pop();
    const int d = (*dist)[cur];
    if (d >= max_hops) {
      continue;
    }
    std::vector<tuide::RegistryNeighbor> nbs;
    std::string err;
    if (!tuide::registry_neighbors(r, cur, {}, "both", &nbs, &err)) {
      continue;
    }
    for (const auto& nb : nbs) {
      if (nb.node.id.empty() || !nb.node.tombstone_reason.empty()) {
        continue;
      }
      if (nb.node.kind == "ctrl") {
        continue;
      }
      if (dist->count(nb.node.id) != 0) {
        continue;
      }
      (*dist)[nb.node.id] = d + 1;
      q.push(nb.node.id);
      if (static_cast<int>(dist->size()) >= cap) {
        break;
      }
    }
  }
}

bool cerca_score_embeddings(tuide::EffectRegistry* r, const std::vector<float>& qvec,
                            const std::string& model_key,
                            const std::unordered_map<std::string, int>* allowed,
                            const std::vector<std::string>& in_scopes,
                            std::vector<tuide::WaveCercaHit>* hits, std::string* err) {
  if (r == nullptr || r->db == nullptr || hits == nullptr || qvec.empty()) {
    if (err) {
      *err = "cerca: score args";
    }
    return false;
  }
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(r->db,
                         "SELECT e.node_id, e.blob, n.kind, n.path, n.symbol, n.stem, n.card_json "
                         "FROM embeddings e JOIN nodes n ON n.id=e.node_id "
                         "WHERE e.model=?1 AND n.tombstone_reason=''",
                         -1, &st, nullptr) != SQLITE_OK) {
    if (err) {
      *err = "cerca: embeddings";
    }
    return false;
  }
  sqlite3_bind_text(st, 1, model_key.c_str(), -1, SQLITE_TRANSIENT);
  struct Scored {
    tuide::RegistryNodeRow node;
    float cos = 0.f;
    int hop = 0;
  };
  std::vector<Scored> scored;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* idp = sqlite3_column_text(st, 0);
    if (idp == nullptr) {
      continue;
    }
    const std::string id = reinterpret_cast<const char*>(idp);
    auto col = [&](int i) -> std::string {
      const unsigned char* p = sqlite3_column_text(st, i);
      return p ? reinterpret_cast<const char*>(p) : "";
    };
    const std::string kind = col(2);
    if (kind == "ctrl") {
      continue;
    }
    tuide::RegistryNodeRow n;
    n.id = id;
    n.kind = kind;
    n.path = col(3);
    n.symbol = col(4);
    n.stem = col(5);
    n.card_json = col(6);
    int hop = 0;
    if (allowed != nullptr) {
      const auto it = allowed->find(id);
      if (it == allowed->end()) {
        continue;
      }
      hop = it->second;
    } else if (!cerca_scope_match(n, in_scopes)) {
      continue;
    }
    const std::vector<float> v =
        cerca_blob_vec(sqlite3_column_blob(st, 1), sqlite3_column_bytes(st, 1));
    Scored s;
    s.node = std::move(n);
    s.cos = tuide::cosine_similarity(qvec, v);
    s.hop = hop;
    scored.push_back(std::move(s));
  }
  sqlite3_finalize(st);
  std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
    return a.cos > b.cos;
  });
  std::unordered_map<std::string, int> stem_n;
  std::unordered_map<std::string, int> scope_n;
  const int per_scope = in_scopes.size() >= 2 ? 3 : 8;
  const int per_stem = in_scopes.size() >= 2 ? 8 : 3;
  for (const auto& s : scored) {
    if (static_cast<int>(hits->size()) >= tuide::kWaveCercaMaxHits) {
      break;
    }
    if (in_scopes.size() >= 2) {
      bool room = false;
      for (const auto& sc : in_scopes) {
        if (cerca_scope_match_one(s.node, sc) && scope_n[sc] < per_scope) {
          room = true;
          break;
        }
      }
      if (!room) {
        continue;
      }
      for (const auto& sc : in_scopes) {
        if (cerca_scope_match_one(s.node, sc)) {
          ++scope_n[sc];
        }
      }
    } else {
      const std::string stem = s.node.stem;
      if (!stem.empty() && stem_n[stem] >= per_stem) {
        continue;
      }
      if (!stem.empty()) {
        ++stem_n[stem];
      }
    }
    tuide::WaveCercaHit w;
    w.id = s.node.id;
    w.path = s.node.path;
    w.symbol = s.node.symbol;
    w.stem = s.node.stem;
    w.kind = s.node.kind;
    w.cosine = s.cos;
    w.hop = s.hop;
    w.card = cerca_clip_card(
        tuide::registry_card_passage(s.node, tuide::RegistryMatchSurface::CardAttrs));
    hits->push_back(std::move(w));
  }
  return true;
}

int run_wave_control_shot(const std::string& root, int argc, char** argv) {
  std::string out_arg;
  std::string from_arg;
  std::string model_id;
  bool dry = false;
  tuide::WaveControlCue cue = tuide::WaveControlCue::Neutral;
  tuide::L2ThinkLevel think_level = tuide::L2ThinkLevel::Medium;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) {
      out_arg = argv[++i];
    } else if (a == "--from" && i + 1 < argc) {
      from_arg = argv[++i];
    } else if (a == "--model-id" && i + 1 < argc) {
      model_id = argv[++i];
    } else if (a == "--dry") {
      dry = true;
    } else if (a == "--cue" && i + 1 < argc) {
      const std::string c = argv[++i];
      if (c == "neutral") {
        cue = tuide::WaveControlCue::Neutral;
      } else if (c == "default") {
        cue = tuide::WaveControlCue::Default;
      } else {
        std::cerr << "wave-control-shot: --cue default|neutral (no '" << c << "')\n";
        return 2;
      }
    } else if (a == "--think" && i + 1 < argc) {
      const std::string t = argv[++i];
      if (t == "off") {
        think_level = tuide::L2ThinkLevel::Off;
      } else if (t == "low") {
        think_level = tuide::L2ThinkLevel::Low;
      } else if (t == "medium") {
        think_level = tuide::L2ThinkLevel::Medium;
      } else if (t == "high") {
        think_level = tuide::L2ThinkLevel::High;
      } else {
        std::cerr << "wave-control-shot: --think off|low|medium|high (no '" << t << "')\n";
        return 2;
      }
    } else if (a == "-h" || a == "--help") {
      std::cerr << "wave-control-shot --out DIR [--from DIR] [--cue default|neutral]\n"
                   "  [--think off|low|medium|high] [--dry] [--model-id ID]\n"
                   "  1× LLM del piloto de control sobre una foto fija (ancla + T1 + circuito +\n"
                   "  examen + atlas). No lanza hijos. Por defecto --cue neutral (sin receta).\n"
                   "  --from: carpeta con ancla.txt jobs.md circuit.md exam.md atlas.md\n"
                   "  Sin --from: tests/fixtures/l2_wave/control_t1_escape_hyp\n";
      return 2;
    }
  }
  if (out_arg.empty()) {
    std::cerr << "wave-control-shot: requiere --out DIR\n";
    return 2;
  }
  fs::path from_dir(from_arg.empty()
                        ? (fs::path(root) / "tests/fixtures/l2_wave/control_t1_escape_hyp")
                        : fs::path(from_arg));
  if (!from_dir.is_absolute()) {
    from_dir = fs::path(root) / from_dir;
  }
  fs::path output_root(out_arg);
  if (!output_root.is_absolute()) {
    output_root = fs::path(root) / output_root;
  }
  const std::string ancla = read_file(from_dir / "ancla.txt");
  const std::string jobs_md = read_file(from_dir / "jobs.md");
  if (ancla.empty() || jobs_md.empty()) {
    std::cerr << "wave-control-shot: falta ancla.txt o jobs.md en " << from_dir << "\n";
    return 2;
  }
  auto trim_nl = [](std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
      s.pop_back();
    }
    return s;
  };
  const std::string atlas = read_file(from_dir / "atlas.md");
  const std::string circuit_md = read_file(from_dir / "circuit.md");
  const std::string exam_md = read_file(from_dir / "exam.md");
  tuide::Catalog shot_cat;
  std::string shot_cerr;
  std::string plano_md;
  if (tuide::kWaveControlCatalogLive && tuide::catalog_ensure(root, &shot_cat, &shot_cerr)) {
    tuide::catalog_apply_heat(&shot_cat, std::string{});
    plano_md = tuide::catalog_render_plano(shot_cat);
  }
  const auto legal = tuide::wave_control_legal({}, 1, true);
  tuide::L2BrainRequest creq;
  creq.system_prompt = tuide::wave_control_system_prompt(cue);
  creq.user_prompt = tuide::wave_control_user_prompt(trim_nl(ancla), jobs_md, atlas, "", {}, legal,
                                                    circuit_md, exam_md, cue, {}, plano_md);
  creq.phase = "causal_wave_control";
  creq.max_tokens = 768;
  creq.temperature = 0.1f;
  const tuide::L2ThinkProfile cthink = tuide::think_profile(think_level);
  tuide::apply_think_profile(&creq, cthink);
  fs::create_directories(output_root);
  std::ofstream(output_root / "system.txt") << creq.system_prompt;
  std::ofstream(output_root / "user.md") << creq.user_prompt;
  std::ofstream(output_root / "think.txt")
      << tuide::l2_think_level_name(cthink.level) << " budget=" << cthink.budget
      << " enable=" << (cthink.enable_thinking ? "yes" : "no") << " cue="
      << (cue == tuide::WaveControlCue::Neutral ? "neutral" : "default") << "\n";
  std::cout << "======== wave-control-shot ========\n"
            << "from=" << from_dir.string() << " cue="
            << (cue == tuide::WaveControlCue::Neutral ? "neutral" : "default")
            << " think=" << tuide::l2_think_level_name(cthink.level)
            << " prompt_chars=" << creq.system_prompt.size() + creq.user_prompt.size() << "\n";
  if (dry) {
    std::cout << creq.user_prompt << "\ndry: no LLM\n";
    return 0;
  }
  AiSettings settings = load_ai_settings(root);
  if (!model_id.empty()) {
    settings.level2.model_id = model_id;
    settings.level2.model_path.clear();
  }
  creq.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  auto brain_ptr = ready_l2_brain(settings, progress, &err);
  if (!brain_ptr) {
    std::cerr << "wave-control-shot: " << err << "\n";
    return err.find("mode") != std::string::npos ? 2 : 1;
  }
  tuide::L2Brain& brain = *brain_ptr;
  std::cout << "L2 ▸ wave-control-shot (" << brain.name() << ")…\n";
  tuide::WaveControlOla cola;
  bool propose_ok = false;
  for (int attempt = 1; attempt <= 2; ++attempt) {
    const auto cresp = brain.propose(creq, nullptr);
    propose_ok = cresp.ok;
    const fs::path adir = output_root / ("attempt_" + std::to_string(attempt));
    fs::create_directories(adir);
    std::ofstream(adir / "raw.txt") << (cresp.raw.empty() ? cresp.text : cresp.raw);
    std::ofstream(adir / "user.md") << creq.user_prompt;
    cola = tuide::WaveControlOla{};
    if (!cresp.ok) {
      std::ofstream(adir / "error.txt") << cresp.error;
      cola.error = cresp.error.empty() ? "control propose falló" : cresp.error;
    } else {
      cola = tuide::wave_parse_control(cresp.text);
      if (!cola.ok && !cresp.raw.empty()) {
        auto again = tuide::wave_parse_control(cresp.raw);
        if (again.ok) {
          cola = std::move(again);
        }
      }
    }
    if (cola.ok && !tuide::wave_control_do_allowed(legal, cola.do_kind)) {
      cola.ok = false;
      cola.error = std::string("control: do no legal ahora (") +
                   tuide::wave_control_do_name(cola.do_kind) + ")";
    }
    if (cola.ok && cola.do_kind == tuide::WaveControlDo::Explorar) {
      for (const auto& c : cola.consultas) {
        std::string derr;
        if (!tuide::wave_control_consulta_ok(c, &derr)) {
          cola.ok = false;
          cola.error = derr.empty() ? "consulta inválida" : derr;
          break;
        }
      }
    }
    const char* do_name = tuide::wave_control_do_name(cola.do_kind);
    nlohmann::json cola_j = {{"ok", cola.ok},
                             {"do", do_name},
                             {"consulta", cola.consulta},
                             {"consultas", cola.consultas},
                             {"ids", cola.ids},
                             {"hacia", cola.hacia},
                             {"plan", tuide::wave_control_plan_to_json(cola.plan)},
                             {"why", cola.why},
                             {"error", cola.error},
                             {"attempt", attempt},
                             {"cue", cue == tuide::WaveControlCue::Neutral ? "neutral" : "default"}};
    std::ofstream(adir / "ola.json") << json_dump_safe(cola_j) << "\n";
    std::ofstream(output_root / "raw.txt") << (cresp.raw.empty() ? cresp.text : cresp.raw);
    std::ofstream(output_root / "ola.json") << json_dump_safe(cola_j) << "\n";
    if (cola.ok || cola.error.find("rama") == std::string::npos || attempt == 2) {
      break;
    }
    const std::string nudge = tuide::wave_control_rama_nudge(cola.consulta);
    std::ofstream(output_root / "nudge.txt") << nudge << "\n";
    creq.user_prompt += "\n" + nudge + "\n";
    std::cerr << "wave-control-shot: mezcla de ramas; reintento con plan\n";
  }
  const char* do_name = tuide::wave_control_do_name(cola.do_kind);
  std::cout << "do=" << do_name << " ok=" << (cola.ok ? "yes" : "no");
  if (!cola.consulta.empty()) {
    std::cout << " consulta=" << cola.consulta;
  }
  if (!cola.plan.fases.empty()) {
    std::cout << " fases=" << cola.plan.fases.size();
  }
  if (!cola.why.empty()) {
    std::cout << " why=" << cola.why;
  }
  if (!cola.error.empty()) {
    std::cout << " error=" << cola.error;
  }
  std::cout << "\n";
  return propose_ok ? 0 : 1;
}

int run_wave_explore(const std::string& root, int argc, char** argv) {
  std::string out_arg;
  std::string case_id;
  std::string prompt;
  std::string cards_arg;
  std::string cards_from;
  int max_waves = tuide::kWaveMaxWaves;
  bool think_override = false;
  bool close_audit = false;
  bool control_mode = false;
  bool control_run_jobs = false;
  std::string script_arg;
  tuide::WaveControlInduce control_induce = tuide::WaveControlInduce::MissAnswer;
  tuide::WaveControlDiet control_diet = tuide::WaveControlDiet::Minimal;
  tuide::L2ThinkLevel think_level = tuide::L2ThinkLevel::Medium;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) {
      out_arg = argv[++i];
    } else if (a == "--case" && i + 1 < argc) {
      case_id = argv[++i];
    } else if (a == "--prompt" && i + 1 < argc) {
      prompt = argv[++i];
    } else if (a == "--cards" && i + 1 < argc) {
      cards_arg = argv[++i];
    } else if (a == "--cards-from" && i + 1 < argc) {
      cards_from = argv[++i];
    } else if (a == "--max-waves" && i + 1 < argc) {
      max_waves = std::max(1, std::atoi(argv[++i]));
    } else if (a == "--think" && i + 1 < argc) {
      const std::string t = argv[++i];
      think_override = true;
      if (t == "off") {
        think_level = tuide::L2ThinkLevel::Off;
      } else if (t == "low") {
        think_level = tuide::L2ThinkLevel::Low;
      } else if (t == "medium") {
        think_level = tuide::L2ThinkLevel::Medium;
      } else if (t == "high") {
        think_level = tuide::L2ThinkLevel::High;
      } else if (t == "close") {
        think_level = tuide::L2ThinkLevel::Off;
        close_audit = true;
      } else {
        std::cerr << "wave-explore: --think off|low|medium|high|close (no '" << t << "')\n";
        return 2;
      }
    } else if (a == "--control") {
      control_mode = true;
    } else if (a == "--run-jobs") {
      control_run_jobs = true;
    } else if (a == "--script" && i + 1 < argc) {
      script_arg = argv[++i];
    } else if (a == "--induce" && i + 1 < argc) {
      const std::string name = argv[++i];
      if (!tuide::wave_control_induce_parse(name, &control_induce)) {
        std::cerr << "wave-explore: --induce baseline|refute-shot|miss-answer|both (no '"
                  << name << "')\n";
        return 2;
      }
    } else if (a == "--control-diet" && i + 1 < argc) {
      const std::string name = argv[++i];
      if (!tuide::wave_control_diet_parse(name, &control_diet)) {
        std::cerr << "wave-explore: --control-diet full|minimal (no '" << name << "')\n";
        return 2;
      }
    } else if (a == "-h" || a == "--help") {
      std::cerr << "wave-explore --out DIR [--case ID|--prompt TEXT] [--max-waves N]\n"
                   "  [--cards FILE | --cards-from DIR] [--think off|low|medium|high|close]\n"
                   "  [--control] [--run-jobs] [--script FILE] [--induce NAME] [--control-diet full|minimal]\n"
                   "  Piloto adaptativo: needles | cerca | juicio | peek | follow | entre | independiente | cerrar.\n"
                   "  Una ola por propose.\n"
                   "  --control: piloto despierta con fichas M*; puede ampliar y luego explorar o plan.\n"
                   "  Por defecto se detiene al emitir explorar (escribe plan.json). --run-jobs lanza los hijos.\n"
                   "  --think fija un nivel para todas las olas (off|low|medium|high).\n"
                   "  --think close: piloto=off; el primer cerrar se re-lanza 1× con medium "
                   "(cerrar, peek del hueco, o follow de un locus anclado). "
                   "Gesto inválido → se aplica el borrador.\n"
                   "  Sin --think: cover=off, piloto=off; si una ola no avanza, la siguiente es medium.\n"
                   "  --cards / --cards-from siembra el cuaderno con fichas de zona (hipótesis).\n"
                   "  --case lee tests/fixtures/stem_boost_battery/prompts_nl_human.json\n"
                   "  --script FILE: sin piloto; lanza esas consultas (una por línea, máx 3).\n"
                   "  --induce: inducción al piloto (no recorta do). miss-answer (defecto) | "
                   "baseline | refute-shot | both.\n"
                   "  --control-diet: full (sense16 largo) | minimal (default S0: prompt corto, sin Sello/Gate).\n";
      return 2;
    }
  }
  if (out_arg.empty()) {
    std::cerr << "wave-explore: requiere --out DIR\n";
    return 2;
  }
  tuide::wave_control_set_diet(control_diet);
  std::cerr << "wave-explore: control-diet=" << tuide::wave_control_diet_name(control_diet)
            << "\n";
  if (prompt.empty() && !case_id.empty()) {
    const fs::path prompts_path =
        fs::path(root) / "tests/fixtures/stem_boost_battery/prompts_nl_human.json";
    std::ifstream in(prompts_path);
    if (!in) {
      std::cerr << "wave-explore: no se pudo leer " << prompts_path << "\n";
      return 1;
    }
    nlohmann::json cases;
    in >> cases;
    for (const auto& item : cases) {
      if (item.value("id", "") == case_id) {
        prompt = item.value("prompt", "");
        break;
      }
    }
    if (prompt.empty()) {
      std::cerr << "wave-explore: caso desconocido " << case_id << "\n";
      return 2;
    }
  }
  if (prompt.empty()) {
    std::cerr << "wave-explore: falta --case o --prompt\n";
    return 2;
  }
  std::vector<std::string> script_consultas;
  if (!script_arg.empty()) {
    if (!control_mode || !control_run_jobs) {
      std::cerr << "wave-explore: --script requiere --control --run-jobs\n";
      return 2;
    }
    std::ifstream sin(script_arg);
    if (!sin) {
      std::cerr << "wave-explore: no se pudo leer " << script_arg << "\n";
      return 1;
    }
    std::ostringstream sraw;
    sraw << sin.rdbuf();
    std::string serr;
    if (!tuide::wave_control_parse_script(sraw.str(), &script_consultas, &serr)) {
      std::cerr << "wave-explore: " << (serr.empty() ? "script inválido" : serr) << "\n";
      return 2;
    }
  }
  fs::path output_root(out_arg);
  if (!output_root.is_absolute()) {
    output_root = fs::path(root) / output_root;
  }
  fs::create_directories(output_root);

  AiSettings settings = load_ai_settings(root);
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  auto brain_ptr = ready_l2_brain(settings, progress, &err);
  if (!brain_ptr) {
    std::cerr << "wave-explore: " << err << "\n";
    return 1;
  }
  tuide::L2Brain& brain = *brain_ptr;
  tuide::EffectRegistry reg;
  if (!open_registry_or_die(root, &reg)) {
    return 1;
  }

  tuide::WaveState state;
  tuide::WaveState* live = &state;
  state.prompt = prompt;
  fs::path cards_path;
  if (!cards_arg.empty()) {
    cards_path = cards_arg;
    if (!cards_path.is_absolute()) {
      cards_path = fs::path(root) / cards_path;
    }
  } else if (!cards_from.empty()) {
    fs::path from(cards_from);
    if (!from.is_absolute()) {
      from = fs::path(root) / from;
    }
    if (from.filename() == "judge_cards.json") {
      cards_path = from;
    } else if (!case_id.empty()) {
      cards_path = from / case_id / "judge_cards.json";
    } else {
      cards_path = from / "judge_cards.json";
    }
  } else if (!case_id.empty()) {
    cards_path = fs::path(root) / ".tuide/ai/l2_explore_battery/round_atlas20_hyp/cards" /
                 case_id / "judge_cards.json";
  }
  nlohmann::json cards_payload;
  if (!cards_path.empty() && fs::exists(cards_path)) {
    std::ifstream cin(cards_path);
    try {
      cin >> cards_payload;
    } catch (const std::exception& ex) {
      std::cerr << "wave-explore: cards JSON inválido: " << ex.what() << "\n";
      tuide::registry_close(&reg);
      return 1;
    }
    const int nseed = tuide::wave_seed_from_atlas(&state, cards_payload);
    std::cerr << "wave-explore: atlas seed " << nseed << " zonas desde " << cards_path << "\n";
    state.atlas_cards = cards_payload;
    state.atlas_md = tuide::registry_causal_atlas_markdown(cards_payload, prompt);
    std::ofstream(output_root / "atlas.md") << state.atlas_md;
  } else if (!cards_arg.empty() || !cards_from.empty()) {
    std::cerr << "wave-explore: no se encontró " << cards_path << "\n";
    tuide::registry_close(&reg);
    return 1;
  }
  tuide::WaveOps ops;
  tuide::EmbeddingBackend embed_backend;
  std::string embed_err;
  ops.search_needle = [&](const std::string& needle, const std::string& campo) {
    tuide::RegistryQueryOpts opts;
    opts.match_surface = tuide::RegistryMatchSurface::NodeId;
    opts.hops = 0;
    opts.top_k = 24;
    opts.max_per_stem = 8;
    if (!campo.empty()) {
      opts.boost_stems = {campo};
    }
    tuide::RegistryQueryResult res;
    std::string qerr;
    auto no_embed = [](bool, const std::string&, std::vector<float>*) { return false; };
    std::vector<tuide::WaveHit> hits;
    if (!tuide::registry_query(&reg, needle, no_embed, opts, &res, &qerr)) {
      std::cerr << "wave-explore: registry_query '" << needle << "': " << qerr << "\n";
      return hits;
    }
    for (const auto& h : res.hits) {
      tuide::WaveHit w;
      w.id = h.node.id;
      w.path = h.node.path;
      w.symbol = h.node.symbol;
      w.stem = h.node.stem;
      w.kind = h.node.kind;
      w.needle = needle;
      hits.push_back(std::move(w));
    }
    return hits;
  };
  ops.search_cerca = [&](const std::string& query,
                         const std::vector<std::pair<std::string, std::string>>& seed_fns,
                         const std::vector<std::string>& boost_stems,
                         const std::vector<std::string>& in_scopes, int hops,
                         std::vector<tuide::WaveCercaHit>* hits, std::string* qerr,
                         std::string* note) {
    (void)boost_stems;
    if (hits == nullptr) {
      if (qerr) {
        *qerr = "hits nulo";
      }
      return false;
    }
    hits->clear();
    if (in_scopes.empty() && seed_fns.empty() && boost_stems.empty()) {
      if (qerr) {
        *qerr = "cerca sin semillas";
      }
      return false;
    }
    if (!embed_backend.ready()) {
      if (!ensure_embed_backend(root, &embed_backend, &embed_err)) {
        if (qerr) {
          *qerr = embed_err.empty() ? "cerca: embed no listo" : embed_err;
        }
        return false;
      }
    }
    auto one = [&](bool is_query, const std::string& text, std::vector<float>* outv) {
      return is_query ? embed_backend.embed_query(text, outv, &embed_err)
                      : embed_backend.embed_passage(text, outv, &embed_err);
    };
    auto many = [&](const std::vector<std::string>& texts, std::vector<std::vector<float>>* outv) {
      return embed_backend.embed_passages(texts, outv, &embed_err);
    };
    if (!in_scopes.empty()) {
      tuide::CercaWakeOpts wopts;
      wopts.query = query;
      wopts.embed = one;
      wopts.embed_many = many;
      wopts.deps.workspace_root = root;
      wopts.deps.search = [root](const std::string& symbol) {
        return harness_rg_symbol(root, symbol);
      };
      tuide::CercaWakeReport wrep;
      std::string werr;
      if (!tuide::registry_wake_cerca_scopes(&reg, in_scopes, wopts, &wrep, &werr)) {
        if (qerr) {
          *qerr = werr.empty() ? "cerca: despertar barrio falló" : werr;
        }
        return false;
      }
      if (note) {
        *note = wrep.note;
      }
      if (!wrep.note.empty() && wrep.note != "wake=0") {
        std::cerr << "wave-explore: cerca " << wrep.note << "\n";
      }
    }
    std::string err2;
    std::vector<float> qvec;
    if (!embed_backend.embed_query(query, &qvec, &err2) || qvec.empty()) {
      if (qerr) {
        *qerr = err2.empty() ? "cerca: embed query falló" : err2;
      }
      return false;
    }
    const std::string model_key = tuide::registry_embed_model_key(
        tuide::kRegistryEmbedModelDefault, tuide::RegistryMatchSurface::CardFull);
    std::unordered_map<std::string, int> dist;
    const std::unordered_map<std::string, int>* allowed = nullptr;
    if (in_scopes.empty()) {
      cerca_bfs_ids(&reg, seed_fns, hops, &dist);
      if (dist.empty()) {
        if (qerr) {
          *qerr = "cerca: barrio vacío";
        }
        return false;
      }
      allowed = &dist;
    }
    if (!cerca_score_embeddings(&reg, qvec, model_key, allowed, in_scopes, hits, qerr)) {
      return false;
    }
    return true;
  };
  ops.peek_code = [&](const std::string& peek, std::string* text, std::string* peek_err) {
    if (text == nullptr) {
      if (peek_err) {
        *peek_err = "text nulo";
      }
      return false;
    }
    const std::string arg = peek;
    const bool file_peek = peek.find(".hpp") != std::string::npos ||
                           peek.find(".cpp") != std::string::npos ||
                           peek.find(".h") != std::string::npos ||
                           peek.find(".cc") != std::string::npos;
    std::string resolved = arg;
    if (!file_peek) {
      if (const tuide::WaveHit* hit = tuide::wave_find_hit(live->candidatas, peek)) {
        if (!hit->path.empty() && !hit->symbol.empty()) {
          resolved = hit->path + ":" + hit->symbol;
        } else if (!hit->path.empty()) {
          resolved = hit->path;
        }
      }
    }
    tuide::GetCodeOfRequest req = tuide::parse_get_code_of_arg(resolved, root);
    req.workspace_root = root;
    if (req.max_lines <= 0) {
      req.max_lines = 120;
    }
    const tuide::GetCodeOfResult got = tuide::get_code_of(req);
    if (!got.ok) {
      if (peek_err) {
        *peek_err = got.error.empty() ? "get_code_of falló" : got.error;
      }
      return false;
    }
    std::ostringstream out;
    if (got.sent_start > 0 && got.sent_end >= got.sent_start) {
      out << (got.path.empty() ? arg : got.path) << ':' << got.sent_start << '-' << got.sent_end;
    } else {
      out << (got.path.empty() ? arg : got.path);
      if (!got.name.empty()) {
        out << ':' << got.name;
      }
    }
    if (!got.name.empty() && got.sent_start > 0) {
      out << " (" << got.name << ")";
    }
    if (got.truncated) {
      out << " [TRUNCATED]";
    }
    out << '\n' << got.text;
    *text = out.str();
    return true;
  };
  ops.peek_causal = [&](const std::string& path, const std::string& symbol, const std::string& body,
                        bool truncated, std::string* md, std::vector<tuide::WaveHit>* callers,
                        std::string* peek_err) {
    if (md == nullptr) {
      if (peek_err) {
        *peek_err = "md nulo";
      }
      return false;
    }
    auto search_fn = [&](const std::string& s) -> std::vector<tuide::ATrailSearchHit> {
      return harness_rg_symbol(root, s);
    };
    std::vector<std::string> incoming;
    const std::string display = path.empty() ? symbol : (path + ":" + symbol);
    std::ostringstream out;
    out << tuide::registry_causal_pilot_aguas_arriba_markdown(root, path, symbol, search_fn,
                                                              &incoming);
    std::string span = body;
    const auto nl = span.find('\n');
    if (nl != std::string::npos) {
      const std::string head = span.substr(0, nl);
      if (head.find('/') != std::string::npos || head.find(".cpp") != std::string::npos ||
          head.find(".hpp") != std::string::npos) {
        span = span.substr(nl + 1);
      }
    }
    out << tuide::registry_causal_pilot_aguas_abajo_markdown(display, span, truncated);
    *md = out.str();
    if (callers != nullptr) {
      for (const auto& t : incoming) {
        tuide::WaveHit w;
        const auto slash = t.find_last_of("/\\");
        const auto colon = t.rfind(':');
        if (colon != std::string::npos && (slash == std::string::npos || colon > slash)) {
          w.path = t.substr(0, colon);
          w.symbol = t.substr(colon + 1);
        } else {
          w.symbol = t;
        }
        w.stem = tuide::registry_stem_of(w.path.empty() ? t : w.path);
        w.kind = "fn";
        w.needle = "follow";
        w.id = w.path.empty() ? w.symbol : (w.path + ":" + w.symbol);
        callers->push_back(std::move(w));
      }
    }
    return true;
  };

  ops.rank_hops = [&](const std::string& query, std::vector<tuide::WavePeekHop>* hops) {
    if (!embed_backend.ready()) {
      (void)ensure_embed_backend(root, &embed_backend, &embed_err);
    }
    wave_rank_peek_hops(&reg, &embed_backend, root, query, hops);
  };

  ops.follow_tree = [&](const std::string& path, const std::string& symbol, std::string* md,
                        std::vector<tuide::WaveHit>* hops, std::string* follow_err) {
    if (md == nullptr) {
      if (follow_err) {
        *follow_err = "md nulo";
      }
      return false;
    }
    if (symbol.empty()) {
      if (follow_err) {
        *follow_err = "follow sin símbolo";
      }
      return false;
    }
    auto search_fn = [&](const std::string& s) -> std::vector<tuide::ATrailSearchHit> {
      return harness_rg_symbol(root, s);
    };
    const std::string display = path.empty() ? symbol : (path + ":" + symbol);
    std::ostringstream out;
    out << "----- follow " << display << " causal -----\n";
    out << harness_causal_flow_markdown(root, path, symbol, search_fn, hops);
    *md = out.str();
    return true;
  };

  ops.path_between = [&](const std::string& from, const std::string& to, std::string* md,
                         std::vector<tuide::WaveHit>* hops, std::string* path_err) {
    if (md == nullptr) {
      if (path_err) {
        *path_err = "md nulo";
      }
      return false;
    }
    auto reg_id = [&](const std::string& loc) -> std::string {
      if (const tuide::WaveHit* hit = tuide::wave_find_hit(live->candidatas, loc)) {
        if (!hit->id.empty()) {
          return hit->id;
        }
        if (!hit->path.empty() && !hit->symbol.empty()) {
          return "fn:" + hit->path + ":" + hit->symbol;
        }
        if (!hit->symbol.empty()) {
          return hit->symbol;
        }
      }
      return loc;
    };
    const std::string src = reg_id(from);
    const std::string dst = reg_id(to);
    std::vector<std::string> ids;
    std::string perr;
    std::ostringstream out;
    out << "----- entre " << from << " → " << to << " -----\n";
    if (!tuide::registry_path_between(&reg, src, dst, &ids, &perr)) {
      out << "(sin camino dirigido en el registry)\n";
      *md = out.str();
      return true;
    }
    std::string chain;
    for (const auto& id : ids) {
      tuide::RegistryNodeRow row;
      std::string gerr;
      tuide::WaveHit w;
      if (tuide::registry_get(&reg, id, &row, &gerr) && !row.id.empty()) {
        w.id = row.id;
        w.path = row.path;
        w.symbol = row.symbol;
        w.stem = row.stem.empty() ? tuide::registry_stem_of(row.path) : row.stem;
        w.kind = row.kind;
      } else {
        w.id = id;
      }
      w.needle = "entre";
      const std::string name = w.symbol.empty() ? w.id : w.symbol;
      if (!chain.empty()) {
        chain += " → ";
      }
      chain += name;
      if (hops != nullptr) {
        hops->push_back(std::move(w));
      }
    }
    out << chain << "\n";
    out << "n=" << ids.size() << " hops (registry, dirigido)\n";
    *md = out.str();
    return true;
  };

  ops.search_in_body = [&](const std::string& path, const std::string& symbol,
                           const std::vector<std::string>& needles, std::string* md,
                           std::vector<int>* hits, std::string* in_err) {
    return harness_in_body_search(root, path, symbol, needles, md, hits, in_err);
  };

  nlohmann::json log = nlohmann::json::array();
  int consecutive_fail = 0;
  int first_circuit_propose = 0;
  int max_user_chars = 0;
  int escalate_n = 0;
  bool escalate_think = false;
  bool close_audit_done = false;
  std::function<void(tuide::WaveState&, const fs::path&, int)> run_waves;
  int indep_serial = 0;
  int control_job_n = 0;
  auto cards_for = [&](const tuide::WaveState& st) -> const nlohmann::json& {
    if (!st.atlas_cards.is_null() && st.atlas_cards.contains("zones")) {
      return st.atlas_cards;
    }
    return cards_payload;
  };
  auto rebuild_atlas_for = [&](const std::string& consulta, tuide::WaveState* w,
                               std::string* aerr) -> bool {
    if (w == nullptr) {
      if (aerr) {
        *aerr = "atlas nulo";
      }
      return false;
    }
    if (!embed_backend.ready()) {
      if (!ensure_embed_backend(root, &embed_backend, &embed_err)) {
        if (aerr) {
          *aerr = embed_err.empty() ? "atlas hijo: embed no listo" : embed_err;
        }
        return false;
      }
    }
    auto one = [&](bool is_query, const std::string& text, std::vector<float>* outv) {
      return is_query ? embed_backend.embed_query(text, outv, &embed_err)
                      : embed_backend.embed_passage(text, outv, &embed_err);
    };
    tuide::RegistryQueryOpts qopts;
    qopts.match_surface = tuide::RegistryMatchSurface::CardFull;
    qopts.hops = 2;
    tuide::RegistryTrailResult trails;
    std::string qerr;
    if (!tuide::registry_query_trails(&reg, consulta, one, qopts, &trails, &qerr)) {
      if (aerr) {
        *aerr = qerr.empty() ? "atlas hijo: trails" : qerr;
      }
      return false;
    }
    tuide::RegistryCausalJudgeOpts jopts;
    tuide::graph_view_profile_apply(
        tuide::graph_view_profile_default(tuide::GraphViewLevel::Atlas), &jopts);
    nlohmann::json payload;
    if (!tuide::registry_causal_judge_payload(&reg, consulta, trails, jopts, &payload, &qerr)) {
      if (aerr) {
        *aerr = qerr.empty() ? "atlas hijo: judge" : qerr;
      }
      return false;
    }
    w->candidatas.clear();
    w->atlas_seed.clear();
    w->opened_ids.clear();
    w->opened_md.clear();
    w->atlas_cards = std::move(payload);
    const int nseed = tuide::wave_seed_from_atlas(w, w->atlas_cards);
    w->atlas_md = tuide::registry_causal_atlas_markdown(w->atlas_cards, consulta);
    std::cerr << "wave-explore: atlas hijo n=" << nseed << " query=" << consulta << "\n";
    return true;
  };
  ops.rebuild_atlas = [&](const std::string& query, tuide::WaveState* child, std::string* ierr) {
    return rebuild_atlas_for(query, child, ierr);
  };
  ops.run_independiente = [&](const std::string&, tuide::WaveState* child, std::string* ierr) {
    if (child == nullptr) {
      if (ierr) {
        *ierr = "hijo nulo";
      }
      return false;
    }
    ++indep_serial;
    const fs::path child_dir = output_root / ("indep_" + std::to_string(indep_serial));
    fs::create_directories(child_dir);
    std::cerr << "wave-explore: independiente #" << indep_serial << " " << child->prompt << "\n";
    if (!child->atlas_cards.is_null() && child->atlas_cards.contains("zones")) {
      std::ofstream(child_dir / "judge_cards.json") << json_dump_safe(child->atlas_cards) << "\n";
      if (!child->atlas_md.empty()) {
        std::ofstream(child_dir / "atlas.md") << child->atlas_md;
      }
    } else if (!cards_payload.is_null() && cards_payload.contains("zones")) {
      if (child->candidatas.empty()) {
        tuide::wave_seed_from_atlas(child, cards_payload);
      }
      const std::string md =
          tuide::registry_causal_atlas_markdown(cards_payload, child->prompt);
      if (!md.empty()) {
        child->atlas_md = md;
      }
    }
    tuide::WaveState* prev = live;
    live = child;
    const bool saved_close_done = close_audit_done;
    const int saved_fail = consecutive_fail;
    const bool saved_esc = escalate_think;
    close_audit_done = false;
    consecutive_fail = 0;
    escalate_think = false;
    run_waves(*child, child_dir, tuide::kWaveIndependienteMaxWaves);
    close_audit_done = saved_close_done;
    consecutive_fail = saved_fail;
    escalate_think = saved_esc;
    live = prev;
    std::ofstream(child_dir / "notebook.md") << tuide::wave_notebook_markdown(*child);
    std::ofstream(child_dir / "state.json") << json_dump_safe(tuide::wave_state_to_json(*child))
                                           << "\n";
    return true;
  };
  if (close_audit) {
    if (control_mode) {
      std::cerr << "wave-explore: think=close (hijos --control: sin re-lanzar el cerrar; "
                   "guion=high; cover/piloto=off)\n";
    } else {
      std::cerr << "wave-explore: think=close (guion=high; cover/piloto=off; "
                   "primer cerrar → 1× medium; cerrar / peek hueco / follow anclado)\n";
    }
  } else if (!think_override) {
    std::cerr << "wave-explore: think=hybrid (guion=high, cover=off, piloto=off, "
                 "atasco→medium; cerrar del piloto siempre vale)\n";
  }
  run_waves = [&](tuide::WaveState& st, const fs::path& waves_root, int waves_cap) {
  for (int i = 0; i < waves_cap && !st.done; ++i) {
    st.propose_n = i + 1;
    const bool guion = tuide::wave_needs_guion(st);
    const bool cover = !guion && tuide::wave_needs_cover(st);
    const bool last_propose = !guion && !cover && st.propose_n >= waves_cap;
    tuide::L2BrainRequest req;
    if (guion) {
      req.system_prompt = tuide::wave_guion_system_prompt();
      req.user_prompt = tuide::wave_guion_user_prompt(st);
      req.phase = "causal_wave_guion";
      req.max_tokens = 384;
    } else if (cover) {
      req.system_prompt = tuide::wave_cover_system_prompt();
      req.user_prompt = tuide::wave_cover_user_prompt(st);
      if (consecutive_fail > 0) {
        req.user_prompt = "Keep 2–3 ids. SOLO el JSON. El primer carácter es `{`. Cero prosa.\n\n" +
                          req.user_prompt;
      }
      req.phase = "causal_wave_cover";
      req.max_tokens = 256;
    } else {
      req.system_prompt = st.control_worker ? tuide::wave_explorer_system_prompt()
                                            : tuide::wave_pilot_system_prompt();
      req.user_prompt = tuide::wave_pilot_user_prompt(st);
      req.phase = "causal_wave_pilot";
      req.max_tokens =
          std::min(512, settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 512);
    }
    const bool escalate_this = !think_override && !cover && !guion && escalate_think;
    escalate_think = false;
    if (escalate_this) {
      ++escalate_n;
      consecutive_fail = 0;
      if (last_propose) {
        req.user_prompt = "Última propose.\n\n" + req.user_prompt;
      } else {
        req.user_prompt = "La ola anterior no avanzó.\n\n" + req.user_prompt;
      }
    }
    req.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
    req.temperature = 0.1f;
    tuide::L2ThinkProfile think;
    if (guion) {
      // CoT de cláusulas; max_tokens reserva JSON tras el budget (apply_think_profile suma).
      // "primer carácter `{`" está prohibido aquí: el think va antes del JSON.
      think = tuide::think_profile(tuide::L2ThinkLevel::High);
    } else if (think_override) {
      think = tuide::think_profile(think_level);
    } else if (cover) {
      // Qwen3.6 with thinking on dumps CoT into content and stops at the
      // think budget — never emits the JSON. Cover is one object; think off.
      think = tuide::think_profile(tuide::L2ThinkLevel::Off);
    } else if (escalate_this) {
      think = tuide::think_profile(tuide::L2ThinkLevel::Medium);
    } else {
      think = tuide::think_profile(tuide::L2ThinkLevel::Off);
    }
    tuide::apply_think_profile(&req, think);
    const fs::path wave_dir = waves_root / ("wave_" + std::to_string(i + 1));
    fs::create_directories(wave_dir);
    std::ofstream(wave_dir / "system.txt") << req.system_prompt;
    std::ofstream(wave_dir / "user.md") << req.user_prompt;
    std::ofstream(wave_dir / "think.txt")
        << tuide::l2_think_level_name(think.level) << " budget=" << think.budget
        << " enable=" << (think.enable_thinking ? "yes" : "no") << "\n";
    max_user_chars = std::max(max_user_chars, static_cast<int>(req.user_prompt.size()));
    const auto response = brain.propose(req, nullptr);
    std::ofstream(wave_dir / "raw.txt") << (response.raw.empty() ? response.text : response.raw);
    auto force_cerrar = [&]() {
      tuide::WaveOla cl;
      cl.ok = true;
      cl.do_kind = tuide::WaveDo::Cerrar;
      cl.why = tuide::wave_circuit_cierre(st);
      if (cl.why.size() < 8) {
        cl.why = "presupuesto agotado; cierre del circuito visto";
      }
      if (tuide::kWaveJobVeredictoEstructurado && st.control_worker) {
        cl.veredicto = "no_concluyente";
      }
      return cl;
    };
    if (!response.ok) {
      std::ofstream(wave_dir / "error.txt") << response.error;
      consecutive_fail++;
      st.last_error = response.error.empty() ? "propose falló" : response.error;
      if (last_propose && !st.done) {
        std::string cperr;
        const auto cl = force_cerrar();
        (void)tuide::wave_apply(&st, cl, ops, &cperr);
      }
      log.push_back({{"wave", i + 1},
                     {"job", control_job_n},
                     {"phase", req.phase},
                     {"ok", false},
                     {"error", st.last_error}});
      if (st.done) {
        break;
      }
      if (guion || consecutive_fail >= 3) {
        break;
      }
      continue;
    }
    tuide::WaveOla ola = tuide::wave_parse_ola(response.text);
    if (guion && !(ola.ok && ola.do_kind == tuide::WaveDo::Guion)) {
      auto salv = tuide::wave_salvage_guion(response.text);
      if (!(salv.ok && salv.do_kind == tuide::WaveDo::Guion) && !response.raw.empty()) {
        salv = tuide::wave_salvage_guion(response.raw);
      }
      if (salv.ok && salv.do_kind == tuide::WaveDo::Guion) {
        ola = std::move(salv);
      }
    }
    std::vector<std::string> atlas_ids;
    for (const auto& h : st.candidatas) {
      if (!h.id.empty() && h.needle == "atlas") {
        atlas_ids.push_back(h.id);
      }
    }
    if (cover && !ola.ok) {
      const auto cov = tuide::registry_parse_causal_atlas_cover(response.text, atlas_ids, {});
      if (cov.ok && !cov.add.empty()) {
        ola = {};
        ola.ok = true;
        ola.do_kind = tuide::WaveDo::Juicio;
        ola.keep = cov.add;
        ola.why = cov.why.size() >= 8 ? cov.why : std::string("ampliar fichas del objeto");
      }
    }
    if (cover && !ola.ok) {
      const auto salvaged = tuide::wave_salvage_keep(response.text, atlas_ids);
      if (!salvaged.empty()) {
        ola = {};
        ola.ok = true;
        ola.do_kind = tuide::WaveDo::Juicio;
        ola.keep = salvaged;
        ola.why = "keep recuperado de una respuesta truncada";
      }
    }
    auto fallback_cover_keep = [&]() {
      std::vector<std::pair<int, std::string>> scored;
      for (const auto& zone : cards_for(st).value("zones", nlohmann::json::array())) {
        const std::string id = zone.value("id", "");
        if (id.empty()) {
          continue;
        }
        const std::string kind = tuide::registry_causal_zone_kind(zone);
        if (kind == "chrome" || kind == "hole") {
          continue;
        }
        int ov = tuide::registry_causal_query_zone_overlap(st.prompt, zone);
        if (kind == "latch" || kind == "object") {
          ov += 10;
        } else if (kind == "caller") {
          ov += 3;
        }
        scored.push_back({ov, id});
      }
      std::sort(scored.begin(), scored.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });
      std::vector<std::string> keep;
      for (const auto& row : scored) {
        keep.push_back(row.second);
        if (static_cast<int>(keep.size()) >= 2) {
          break;
        }
      }
      return keep;
    };
    if (cover && !ola.ok) {
      const auto keep = fallback_cover_keep();
      if (!keep.empty()) {
        ola = {};
        ola.ok = true;
        ola.do_kind = tuide::WaveDo::Juicio;
        ola.keep = keep;
        ola.why = "fallback: cover sin JSON, se abren owns de mayor solape";
      }
    }
    bool did_close_audit = false;
    if (close_audit && !st.control_worker && !close_audit_done && !cover && !guion && ola.ok &&
        ola.do_kind == tuide::WaveDo::Cerrar && !think.enable_thinking) {
      close_audit_done = true;
      did_close_audit = true;
      const tuide::WaveOla draft = ola;
      std::ofstream(wave_dir / "cerrar_draft.json")
          << json_dump_safe(tuide::wave_ola_to_json(draft)) << "\n";
      tuide::L2BrainRequest audit_req = req;
      audit_req.user_prompt =
          "Revisión de cierre (UNA tirada). Borrador:\n" + json_dump_safe(tuide::wave_ola_to_json(draft), 0) +
          "\n\n" + tuide::wave_close_audit_instructions(st) +
          "No reinicies. No recites el atlas. No abras un plan de varias olas.\n\n" +
          req.user_prompt;
      think = tuide::think_profile(tuide::L2ThinkLevel::Medium);
      tuide::apply_think_profile(&audit_req, think);
      std::ofstream(wave_dir / "audit_user.md") << audit_req.user_prompt;
      std::ofstream(wave_dir / "audit_think.txt")
          << tuide::l2_think_level_name(think.level) << " budget=" << think.budget
          << " enable=" << (think.enable_thinking ? "yes" : "no") << "\n";
      const auto audit_response = brain.propose(audit_req, nullptr);
      std::ofstream(wave_dir / "audit_raw.txt")
          << (audit_response.raw.empty() ? audit_response.text : audit_response.raw);
      if (audit_response.ok) {
        auto audit_ola = tuide::wave_parse_ola(audit_response.text);
        if (audit_ola.ok && tuide::wave_close_audit_accept(draft, audit_ola, st)) {
          ola = std::move(audit_ola);
        } else {
          ola = draft;
          std::cerr << "wave " << (i + 1) << " close-audit ignorado; se aplica el borrador\n";
        }
      } else {
        std::ofstream(wave_dir / "audit_error.txt") << audit_response.error;
        ola = draft;
      }
      std::cerr << "wave " << (i + 1) << " close-audit do=" << tuide::wave_do_name(ola.do_kind)
                << " think=medium\n";
    }
    std::ofstream(wave_dir / "ola.json") << json_dump_safe(tuide::wave_ola_to_json(ola)) << "\n";
    std::string apply_err;
    bool ok = false;
    if (guion && ola.ok && ola.do_kind != tuide::WaveDo::Guion) {
      apply_err = "ola 0: solo guion (papeles de la consulta)";
      st.last_error = apply_err;
    } else if (cover && ola.ok && ola.do_kind != tuide::WaveDo::Juicio) {
      apply_err = "ola 0: solo juicio keep (ampliar fichas)";
      st.last_error = apply_err;
    } else if (cover && ola.ok &&
               (ola.keep.empty() ||
                static_cast<int>(ola.keep.size()) < tuide::kWaveCoverKeepMin)) {
      apply_err = "ola 0: keep 2–3 ids a ampliar";
      st.last_error = apply_err;
    } else {
      if (cover && static_cast<int>(ola.keep.size()) > tuide::kWaveCoverKeepMax) {
        ola.keep.resize(static_cast<size_t>(tuide::kWaveCoverKeepMax));
      }
      ok = tuide::wave_apply(&st, ola, ops, &apply_err);
    }
    if (ok && cover) {
      st.opened_ids = ola.keep;
      std::string opened = tuide::registry_causal_pilot_opened_pack(cards_for(st), ola.keep, st.prompt);
      const auto filtered = tuide::registry_causal_payload_filter_zones(cards_for(st), ola.keep);
      const std::string inspect =
          tuide::registry_causal_pack_markdown(filtered, tuide::GraphViewLevel::Inspect);
      if (!inspect.empty()) {
        opened += "\n";
        opened += inspect;
      }
      constexpr std::size_t kOpenedCap = 7000;
      if (opened.size() > kOpenedCap) {
        opened.resize(kOpenedCap);
        opened += "\n…\n";
      }
      st.opened_md = std::move(opened);
      tuide::wave_retain_atlas_ids(&st, ola.keep);
      tuide::wave_ingest_zone_symbols(&st, cards_for(st), ola.keep);
    }
    if (ok && last_propose && !st.done && ola.do_kind != tuide::WaveDo::Cerrar) {
      std::string cperr;
      const auto cl = force_cerrar();
      (void)tuide::wave_apply(&st, cl, ops, &cperr);
    }
    if (!ok && last_propose && !st.done) {
      ola = force_cerrar();
      ok = tuide::wave_apply(&st, ola, ops, &apply_err);
    }
    if (!ok && !think_override && !cover && !guion && !last_propose &&
        !tuide::wave_circuit_complete(st)) {
      escalate_think = true;
    }
    if (first_circuit_propose == 0 && tuide::wave_circuit_complete(st)) {
      first_circuit_propose = st.propose_n;
    }
    std::ofstream(wave_dir / "work.md") << tuide::wave_work_markdown(st);
    std::ofstream(wave_dir / "notebook.md") << tuide::wave_notebook_markdown(st);
    std::ofstream(wave_dir / "state.json") << json_dump_safe(tuide::wave_state_to_json(st)) << "\n";
    log.push_back({{"wave", i + 1},
                   {"job", control_job_n},
                   {"propose", st.propose_n},
                   {"phase", req.phase},
                   {"ok", ok},
                   {"do", tuide::wave_do_name(ola.do_kind)},
                   {"why", ola.why},
                   {"error", ok ? "" : apply_err},
                   {"user_chars", static_cast<int>(req.user_prompt.size())},
                   {"circuit", tuide::wave_circuit_complete(st)},
                   {"think", tuide::l2_think_level_name(think.level)},
                   {"think_budget", think.budget},
                   {"escalate", escalate_this},
                   {"close_audit", did_close_audit}});
    std::cerr << "wave " << (i + 1) << " phase=" << req.phase
              << " do=" << tuide::wave_do_name(ola.do_kind) << " ok=" << (ok ? "yes" : "no")
              << " think=" << tuide::l2_think_level_name(think.level) << " budget=" << think.budget
              << " user_chars=" << req.user_prompt.size()
              << " circuit=" << (tuide::wave_circuit_complete(st) ? "yes" : "no");
    if (!ok) {
      std::cerr << " err=" << apply_err;
    }
    std::cerr << "\n";
    if (ok) {
      consecutive_fail = 0;
    } else if (guion) {
      std::cerr << "wave-explore: guion falló; no se reintenta el LLM\n";
      break;
    } else if (++consecutive_fail >= 3) {
      break;
    }
  }
  };
  if (control_mode) {
    std::cerr << "wave-explore: --control (";
    if (!script_consultas.empty()) {
      std::cerr << "script " << script_consultas.size() << " trabajos, sin piloto";
    } else {
      std::cerr << "piloto despierta con fichas; "
                << (control_run_jobs ? "lanza exploradores" : "para al explorar");
    }
    std::cerr << "; max " << tuide::kWaveControlMaxJobsHard << " trabajos (floor "
              << tuide::kWaveControlMaxJobs << "); induce="
              << tuide::wave_control_induce_name(control_induce) << ")\n";
    const std::string user_consulta = prompt;
    tuide::Catalog control_cat;
    if (tuide::kWaveControlCatalogLive) {
      std::string cat_err;
      if (!tuide::catalog_ensure(root, &control_cat, &cat_err)) {
        std::cerr << "wave-explore: catalog " << cat_err << "\n";
      } else {
        std::cerr << "wave-explore: catalog " << (control_cat.cache_hit ? "hit" : "rebuild")
                  << " barrios=" << control_cat.barrios.size()
                  << " stems=" << control_cat.stems.size() << "\n";
      }
    }
    if (!cards_payload.is_object() || !cards_payload.contains("zones") ||
        !cards_payload["zones"].is_array() || cards_payload["zones"].empty()) {
      tuide::WaveState seed_st;
      std::string aerr;
      if (rebuild_atlas_for(user_consulta, &seed_st, &aerr) && seed_st.atlas_cards.is_object()) {
        cards_payload = std::move(seed_st.atlas_cards);
        const auto n =
            cards_payload.contains("zones") && cards_payload["zones"].is_array()
                ? cards_payload["zones"].size()
                : 0;
        std::cerr << "wave-explore: control fichas n=" << n << " (retrieval)\n";
      } else {
        std::cerr << "wave-explore: control sin fichas (" << aerr << ")\n";
      }
    }
    state = tuide::WaveState{};
    state.prompt = user_consulta;
    std::string jobs_md;
    nlohmann::json jobs = nlohmann::json::array();
    nlohmann::json control_turns = nlohmann::json::array();
    bool control_cerró = false;
    std::string control_why;
    int jobs_run = 0;
    std::vector<std::string> prev_consultas;
    std::vector<std::string> failed_consultas;
    std::vector<std::string> last_visto;
    std::vector<std::string> all_visto;
    std::vector<tuide::WaveControlJobSnap> job_snaps;
    std::string circuit_md;
    std::string exam_md;
    auto last_hunt_failed = [&]() {
      return tuide::wave_control_jobs_last_failed(jobs_md) ||
             (jobs_run > 0 && last_visto.empty());
    };
    auto jobs_path_between = [&](const std::string& from, const std::string& to, std::string* md,
                                 std::vector<tuide::WaveHit>* hops, std::string* path_err) -> bool {
      auto to_fn = [](const std::string& loc) {
        if (loc.rfind("fn:", 0) == 0) {
          return loc;
        }
        if (loc.find('/') != std::string::npos) {
          return std::string("fn:") + loc;
        }
        return loc;
      };
      std::vector<std::string> ids;
      std::string perr;
      if (!tuide::registry_path_between(&reg, to_fn(from), to_fn(to), &ids, &perr)) {
        if (path_err) {
          *path_err = perr.empty() ? "sin camino" : perr;
        }
        if (md) {
          *md = "(sin camino)";
        }
        return false;
      }
      if (hops != nullptr) {
        hops->clear();
      }
      std::string chain;
      for (const auto& id : ids) {
        tuide::RegistryNodeRow row;
        std::string gerr;
        tuide::WaveHit w;
        if (tuide::registry_get(&reg, id, &row, &gerr) && !row.id.empty()) {
          w.id = row.id;
          w.path = row.path;
          w.symbol = row.symbol;
          w.stem = row.stem.empty() ? tuide::registry_stem_of(row.path) : row.stem;
          w.kind = row.kind;
        } else {
          w.id = id;
        }
        w.needle = "entre";
        const std::string name = w.symbol.empty() ? w.id : w.symbol;
        if (!chain.empty()) {
          chain += " → ";
        }
        chain += name;
        if (hops != nullptr) {
          hops->push_back(std::move(w));
        }
      }
      if (md) {
        *md = chain;
      }
      return hops != nullptr && hops->size() >= 2;
    };
    auto refresh_circuit = [&]() {
      circuit_md = tuide::wave_control_jobs_circuit_pack(job_snaps, jobs_path_between);
      if (!circuit_md.empty()) {
        std::ofstream f(output_root / "circuit.md");
        f << circuit_md;
        if (circuit_md.back() != '\n') {
          f << '\n';
        }
      }
      exam_md = tuide::wave_control_slice_exam(user_consulta, job_snaps, control_induce);
      if (!exam_md.empty()) {
        std::ofstream f(output_root / "exam.md");
        f << exam_md;
        if (exam_md.back() != '\n') {
          f << '\n';
        }
      }
    };
    tuide::WaveControlPlan control_plan;
    auto seed_control_worker = [&](tuide::WaveState* w, const std::string& consulta,
                                   const tuide::WaveControlLaunch* spec) {
      *w = tuide::WaveState{};
      w->prompt = consulta;
      w->control_worker = true;
      w->independiente_leaf = true;
      // El hijo no ve el ancla: su pack es retrieval de ESA consulta, no el mazo
      // del padre. Si el embed falla, se cae al atlas del ancla (re-pintado).
      std::string aerr;
      if (!rebuild_atlas_for(consulta, w, &aerr)) {
        std::cerr << "wave-explore: atlas hijo falló (" << aerr << "); mazo del ancla\n";
        if (!cards_payload.is_null() && cards_payload.contains("zones") &&
            !cards_payload["zones"].empty()) {
          tuide::wave_seed_from_atlas(w, cards_payload);
          w->atlas_cards = cards_payload;
          w->atlas_md = tuide::registry_causal_atlas_markdown(cards_payload, consulta);
        }
      }
      if (spec != nullptr) {
        tuide::wave_control_seed_launch(w, *spec);
      }
    };
    auto run_explorer_job = [&](const std::string& consulta, const tuide::WaveControlLaunch& spec) {
      tuide::WaveControlLaunch launch = spec;
      if (tuide::wave_control_inherit_visto_ok(jobs_md)) {
        tuide::wave_control_inherit_visto(&launch, all_visto);
      }
      launch.fallos_md = tuide::wave_control_fallos_cue(job_snaps);
      tuide::WaveState worker;
      const bool seed = launch.ok || !launch.pin_loci.empty() || !launch.pin_from.empty() ||
                        !launch.hacia.empty() || !launch.evita.empty() || !launch.fallos_md.empty();
      seed_control_worker(&worker, consulta, seed ? &launch : nullptr);
      const int job = jobs_run + 1;
      const fs::path job_dir = output_root / ("job_" + std::to_string(job));
      fs::create_directories(job_dir);
      if (!worker.atlas_md.empty()) {
        std::ofstream(job_dir / "atlas.md") << worker.atlas_md;
      }
      if (!worker.atlas_cards.is_null() && worker.atlas_cards.contains("zones")) {
        std::ofstream(job_dir / "judge_cards.json") << json_dump_safe(worker.atlas_cards) << "\n";
      }
      std::cerr << "wave-explore: control job #" << job << " " << consulta << "\n";
      control_job_n = job;
      live = &worker;
      close_audit_done = false;
      consecutive_fail = 0;
      escalate_think = false;
      first_circuit_propose = 0;
      run_waves(worker, job_dir, max_waves);
      const std::string brief = tuide::wave_control_brief(worker, control_induce);
      std::ofstream(job_dir / "brief.md") << brief;
      std::ofstream(job_dir / "notebook.md") << tuide::wave_notebook_markdown(worker);
      std::ofstream(job_dir / "state.json") << json_dump_safe(tuide::wave_state_to_json(worker))
                                           << "\n";
      std::ofstream(job_dir / "pack.json") << json_dump_safe(tuide::wave_pack_to_json(worker))
                                          << "\n";
      std::ofstream(job_dir / "pack.md") << tuide::wave_pack_markdown(worker);
      jobs_md += "### Trabajo " + std::to_string(job) + "\n" + brief + "\n";
      if (!state.notas.empty() && state.notas.back() != '\n') {
        state.notas += "\n";
      }
      state.notas += "## Trabajo " + std::to_string(job) + "\n" + brief;
      if (!state.notas.empty() && state.notas.back() != '\n') {
        state.notas += "\n";
      }
      prev_consultas.push_back(consulta);
      last_visto = tuide::wave_pack_handoff(worker).visto;
      if (tuide::wave_control_job_marca_failed(jobs_md, last_visto.empty())) {
        failed_consultas.push_back(consulta);
      }
      jobs.push_back({{"n", job},
                      {"consulta", consulta},
                      {"keep", worker.opened_ids},
                      {"cerró", worker.done},
                      {"proposes", worker.propose_n},
                      {"visto", last_visto}});
      job_snaps.push_back(tuide::wave_control_job_snap(job, worker));
      refresh_circuit();
      for (const auto& v : last_visto) {
        bool have = false;
        for (const auto& e : all_visto) {
          if (e == v) {
            have = true;
            break;
          }
        }
        if (!have && !v.empty()) {
          all_visto.push_back(v);
        }
      }
      ++jobs_run;
      live = &state;
    };
    auto control_atlas_from = [&](const std::vector<std::string>& opened) {
      const auto& cards =
          cards_payload.contains("zones") ? cards_payload : nlohmann::json::object();
      std::vector<std::string> keep;
      std::unordered_set<std::string> skip(opened.begin(), opened.end());
      for (const auto& zone : cards.value("zones", nlohmann::json::array())) {
        const std::string id = zone.value("id", "");
        if (id.empty() || skip.count(id)) {
          continue;
        }
        keep.push_back(id);
      }
      if (keep.empty() && !opened.empty()) {
        return std::string("Atlas del resto: (ninguna ficha compacta)\n");
      }
      const nlohmann::json src =
          opened.empty() ? cards : tuide::registry_causal_payload_filter_zones(cards, keep);
      return tuide::wave_control_atlas_pack_brief(
          tuide::registry_causal_atlas_markdown(src, user_consulta),
          opened.empty() ? 0 : static_cast<std::size_t>(tuide::kWaveControlRestChars));
    };
    std::string control_atlas = control_atlas_from({});
    std::string control_inspect;
    std::string control_bosquejo;
    std::string control_plano;
    std::string control_zoom_md;
    std::string control_nudge;
    std::string control_last_reject;
    int control_rama_retries = 0;
    int control_plantilla_retries = 0;
    std::string control_last_plantilla;
    int control_cerrar_retries = 0;
    int control_cerca_retries = 0;
    int control_recap_retries = 0;
    int control_replay_retries = 0;
    int control_job_cap = tuide::wave_control_job_cap(control_plan);
    int control_open_retries = 0;
    int control_bosquejos = 0;
    int control_agujas_n = 0;
    std::vector<std::string> control_agujas;
    bool control_amplió = false;
    bool control_ampliar_lock = false;
    int control_ampliar_turns = 0;
    std::vector<std::string> control_ampliar_ids;
    std::vector<std::string> control_zoom_ids;
    auto refresh_orientation = [&]() {
      if (control_agujas.empty()) {
        tuide::catalog_apply_heat(&control_cat, std::string{});
      } else {
        tuide::catalog_apply_heat(&control_cat, control_agujas);
      }
      std::ostringstream plano;
      if (!control_agujas.empty()) {
        plano << "luz (calibración del piloto; no copies):";
        for (std::size_t i = 0; i < control_agujas.size(); ++i) {
          plano << (i ? "," : "") << " " << control_agujas[i];
        }
        plano << "\n";
      }
      plano << tuide::catalog_render_plano(control_cat);
      control_plano = plano.str();
      control_zoom_md = tuide::catalog_render_zooms(control_cat, control_zoom_ids);
      if (!control_plano.empty()) {
        std::ofstream(output_root / "plano.md") << control_plano << "\n";
      }
      if (!control_zoom_md.empty()) {
        std::ofstream(output_root / "zoom.md") << control_zoom_md << "\n";
      }
    };
    if (tuide::kWaveControlCatalogLive) {
      refresh_orientation();
    }
    if (!control_atlas.empty()) {
      std::ofstream(output_root / "control_atlas.md") << control_atlas << "\n";
    }
    auto write_plan_files = [&](const std::string& why) {
      std::ostringstream pmd;
      pmd << tuide::wave_control_plan_markdown(control_plan);
      if (!why.empty()) {
        pmd << "why: " << why << "\n";
      }
      std::ofstream(output_root / "plan.md") << pmd.str();
      std::ofstream(output_root / "plan.json")
          << json_dump_safe(tuide::wave_control_plan_to_json(control_plan)) << "\n";
    };
    auto launch_current_phase = [&]() -> bool {
      if (jobs_run >= control_job_cap) {
        control_why = "presupuesto de trabajos agotado";
        return false;
      }
      const int cur = tuide::wave_control_plan_current(control_plan);
      if (cur >= 0) {
        const auto& ph = control_plan.fases[static_cast<std::size_t>(cur)];
        if (ph.consulta.empty()) {
          control_nudge =
              "Esta fase no tiene consulta. revisar: escríbela tú (un polo o si hay camino). "
              "El runtime no la redacta.";
          return true;
        }
      }
      const auto spec = tuide::wave_control_launch_spec(control_plan);
      if (!spec.ok) {
        control_why = "control: fase no lanzable";
        return false;
      }
      run_explorer_job(spec.consulta, spec);
      write_plan_files({});
      return true;
    };
    if (!script_consultas.empty()) {
      for (const auto& c : script_consultas) {
        if (jobs_run >= tuide::kWaveControlMaxJobs) {
          break;
        }
        tuide::WaveControlLaunch spec;
        spec.consulta = c;
        spec.ok = true;
        run_explorer_job(c, spec);
      }
      control_why = "script: " + std::to_string(jobs_run) + " trabajos";
    } else {
    const int max_control_turns =
        tuide::kWaveControlMaxJobs + tuide::kWaveControlLatePlanJobs + 6;
    for (int turn = 1; turn <= max_control_turns; ++turn) {
      tuide::L2BrainRequest creq;
      creq.system_prompt = tuide::wave_control_system_prompt(tuide::WaveControlCue::Default,
                                                            control_induce);
      const int ampliar_legal = control_ampliar_lock ? tuide::kWaveControlMaxAmpliarTurns
                                                    : control_ampliar_turns;
      const auto legal_now =
          tuide::wave_control_legal(control_plan, jobs_run, !last_visto.empty(),
                                    control_agujas_n,
                                    static_cast<int>(control_zoom_ids.size()), ampliar_legal,
                                    control_bosquejos);
      creq.user_prompt = tuide::wave_control_user_prompt(user_consulta, jobs_md, control_atlas,
                                                        control_inspect, control_plan, legal_now,
                                                        circuit_md, exam_md,
                                                        tuide::WaveControlCue::Default,
                                                        control_bosquejo, control_plano,
                                                        control_zoom_md, control_induce,
                                                        control_last_reject);
      if (!control_nudge.empty()) {
        creq.user_prompt += "\n" + control_nudge + "\n";
        control_nudge.clear();
      }
      creq.phase = "causal_wave_control";
      creq.max_tokens = 1024;
      creq.n_ctx = std::max(8192, settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192);
      if (settings.level2.n_ctx_remote > creq.n_ctx) {
        creq.n_ctx = settings.level2.n_ctx_remote;
      }
      creq.temperature = 0.1f;
      const tuide::L2ThinkProfile cthink =
          (think_override && !close_audit)
              ? tuide::think_profile(think_level)
              : tuide::think_profile_for("causal_wave_control", false, false);
      tuide::apply_think_profile(&creq, cthink);
      const fs::path cdir = output_root / ("control_" + std::to_string(turn));
      fs::create_directories(cdir);
      std::ofstream(cdir / "system.txt") << creq.system_prompt;
      std::ofstream(cdir / "user.md") << creq.user_prompt;
      if (!control_plano.empty()) {
        std::ofstream(cdir / "plano.md") << control_plano << "\n";
      }
      if (!control_zoom_md.empty()) {
        std::ofstream(cdir / "zoom.md") << control_zoom_md << "\n";
      }
      std::ofstream(cdir / "think.txt")
          << tuide::l2_think_level_name(cthink.level) << " budget=" << cthink.budget
          << " enable=" << (cthink.enable_thinking ? "yes" : "no") << "\n";
      max_user_chars = std::max(max_user_chars, static_cast<int>(creq.user_prompt.size()));
      const auto cresp = brain.propose(creq, nullptr);
      std::ofstream(cdir / "raw.txt") << (cresp.raw.empty() ? cresp.text : cresp.raw);
      tuide::WaveControlOla cola;
      if (!cresp.ok) {
        std::ofstream(cdir / "error.txt") << cresp.error;
        cola.error = cresp.error.empty() ? "control propose falló" : cresp.error;
      } else {
        cola = tuide::wave_parse_control(cresp.text);
        if (!cola.ok && !cresp.raw.empty()) {
          auto again = tuide::wave_parse_control(cresp.raw);
          if (again.ok) {
            cola = std::move(again);
          }
        }
      }
      if (cola.ok) {
        const auto legal_apply =
            tuide::wave_control_legal(control_plan, jobs_run, !last_visto.empty(),
                                      control_agujas_n,
                                      static_cast<int>(control_zoom_ids.size()), ampliar_legal,
                                      control_bosquejos);
        if (!tuide::wave_control_do_allowed(legal_apply, cola.do_kind)) {
          cola.ok = false;
          cola.error = std::string("control: do no legal ahora (") +
                       tuide::wave_control_do_name(cola.do_kind) + ")";
        }
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Ampliar) {
        if (jobs_run > 0) {
          cola.ok = false;
          cola.error = "control: ampliar solo antes de explorar";
        } else {
          std::unordered_set<std::string> have(control_ampliar_ids.begin(),
                                               control_ampliar_ids.end());
          std::vector<std::string> fresh;
          for (const auto& id : cola.ids) {
            if (have.count(id)) {
              continue;
            }
            bool found = false;
            if (cards_payload.is_object()) {
              for (const auto& zone : cards_payload.value("zones", nlohmann::json::array())) {
                if (zone.is_object() && zone.value("id", "") == id) {
                  found = true;
                  break;
                }
              }
            }
            if (!found) {
              cola.ok = false;
              cola.error = "control: id no está en el atlas";
              break;
            }
            have.insert(id);
            fresh.push_back(id);
          }
          if (cola.ok && fresh.empty()) {
            cola.ok = false;
            cola.error = "control: esa ficha ya está abierta";
          } else if (cola.ok) {
            cola.ids = std::move(fresh);
          }
          if (cola.ok &&
              static_cast<int>(control_ampliar_ids.size() + cola.ids.size()) >
                  tuide::kWaveControlMaxOpened) {
            cola.ok = false;
            cola.error = "control: máx 6 fichas abiertas";
          }
        }
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Zoom) {
        std::unordered_set<std::string> have(control_zoom_ids.begin(), control_zoom_ids.end());
        std::vector<std::string> fresh;
        for (const auto& id : cola.ids) {
          if (have.count(id)) {
            continue;
          }
          if (!tuide::catalog_has_id(control_cat, id)) {
            cola.ok = false;
            cola.error = "control: zoom id no está en el plano";
            break;
          }
          have.insert(id);
          fresh.push_back(id);
        }
        if (cola.ok && fresh.empty()) {
          cola.ok = false;
          cola.error = "control: ese zoom ya está abierto";
        } else if (cola.ok) {
          cola.ids = std::move(fresh);
        }
        if (cola.ok &&
            static_cast<int>(control_zoom_ids.size() + cola.ids.size()) >
                tuide::kWaveControlMaxOpened) {
          cola.ok = false;
          cola.error = "control: máx 6 zooms abiertos";
        }
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Bosquejar) {
        if (control_bosquejos >= tuide::kWaveControlMaxBosquejar) {
          cola.ok = false;
          cola.error = "control: bosquejar máx 2";
        }
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Agujas) {
        if (control_agujas_n >= tuide::kWaveControlMaxAgujas) {
          cola.ok = false;
          cola.error = "control: agujas máx 1";
        } else {
          std::vector<std::string> trial = control_agujas;
          if (tuide::wave_control_agujas_merge(&trial, cola.hacia) <= 0) {
            cola.ok = false;
            cola.error = "control: agujas sin zona nueva";
          }
        }
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Explorar) {
        const auto owns = tuide::wave_control_owns_phrases(cards_payload);
        if (!owns.empty()) {
          for (auto& c : cola.consultas) {
            c = tuide::wave_control_strip_owns(c, owns);
            std::string cerr;
            if (!tuide::wave_control_consulta_ok(c, &cerr)) {
              cola.ok = false;
              cola.error = cerr.empty() ? "consulta inválida" : cerr;
              break;
            }
          }
          if (cola.ok) {
            for (std::size_t i = 0; i < cola.consultas.size(); ++i) {
              for (std::size_t k = i + 1; k < cola.consultas.size(); ++k) {
                if (cola.consultas[i] == cola.consultas[k]) {
                  cola.ok = false;
                  cola.error = "control: consultas repetidas";
                  break;
                }
              }
              if (!cola.ok) {
                break;
              }
            }
          }
          if (cola.ok && !cola.consultas.empty()) {
            cola.consulta = cola.consultas.front();
          }
        }
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Explorar) {
        std::vector<std::string> seen = prev_consultas;
        for (const auto& c : cola.consultas) {
          std::string derr;
          if (tuide::wave_control_consulta_es_ancla(c, user_consulta)) {
            cola.ok = false;
            cola.error = "control: no pases el ancla a un explorador";
            break;
          }
          if (!tuide::wave_control_consulta_delta_ok(c, user_consulta, seen, &derr)) {
            cola.ok = false;
            cola.error = derr.empty() ? "consulta no es un desplazamiento" : derr;
            break;
          }
          if (!tuide::wave_control_consulta_replay_ok(
                  c, prev_consultas, last_hunt_failed(), &derr, failed_consultas)) {
            cola.ok = false;
            cola.error = derr.empty() ? "control: esa caza no encontró el mapeo; cierra el ancla o parte"
                                      : derr;
            break;
          }
          seen.push_back(c);
        }
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Plan) {
        std::vector<std::string> seen = prev_consultas;
        for (const auto& ph : cola.plan.fases) {
          if (ph.kind != tuide::WaveControlPhaseKind::Locator) {
            continue;
          }
          if (ph.consulta.empty()) {
            continue;
          }
          std::string derr;
          if (tuide::wave_control_consulta_es_ancla(ph.consulta, user_consulta)) {
            cola.ok = false;
            cola.error = "control: no pases el ancla a un explorador";
            break;
          }
          if (!tuide::wave_control_consulta_delta_ok(ph.consulta, user_consulta, seen, &derr)) {
            cola.ok = false;
            cola.error = derr.empty() ? "consulta no es un desplazamiento" : derr;
            break;
          }
          if (!tuide::wave_control_consulta_replay_ok(
                  ph.consulta, prev_consultas, last_hunt_failed(),
                  &derr, failed_consultas)) {
            cola.ok = false;
            cola.error = derr.empty() ? "control: esa caza no encontró el mapeo; cierra el ancla o parte"
                                      : derr;
            break;
          }
          seen.push_back(ph.consulta);
        }
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Cerrar) {
        std::string cerr;
        bool gate_fail = last_hunt_failed();
        if (tuide::wave_ctrl(tuide::kWaveControlGateCerrarAnyMiss)) {
          gate_fail = gate_fail || tuide::wave_control_jobs_any_miss(jobs_md);
        }
        const bool dump_gate =
            tuide::wave_ctrl(tuide::kWaveControlGateDumpNoCierra) &&
            (tuide::wave_ctrl(tuide::kWaveControlGateDumpNoCierraAny)
                 ? tuide::wave_control_jobs_any_dump_leido(jobs_md)
                 : tuide::wave_control_jobs_last_dump_leido(jobs_md));
        if (dump_gate) {
          gate_fail = true;
        }
        if (!tuide::wave_control_cerrar_tras_fallo_ok(cola.why, gate_fail, &cerr)) {
          cola.ok = false;
          cola.error = cerr.empty()
                           ? "control: el último Cerrado negó el mapeo; no selles un polo anterior"
                           : cerr;
        } else if (dump_gate &&
                   !tuide::wave_control_cerrar_tras_dump_ok(cola.why, true, &cerr)) {
          cola.ok = false;
          cola.error = cerr.empty()
                           ? "control: un leído no cierra el ancla; no refuta el existe"
                           : cerr;
        } else if (!tuide::wave_control_cerrar_polos_ok(cola, job_snaps, &cerr)) {
          cola.ok = false;
          cola.error = cerr.empty() ? "control: polos inválidos" : cerr;
        }
      }
      const char* do_name = tuide::wave_control_do_name(cola.do_kind);
      nlohmann::json cola_j = {{"ok", cola.ok},
                               {"do", do_name},
                               {"consulta", cola.consulta},
                               {"consultas", cola.consultas},
                               {"ids", cola.ids},
                               {"hacia", cola.hacia},
                               {"evita", cola.evita},
                               {"plan", tuide::wave_control_plan_to_json(cola.plan)},
                               {"why", cola.why},
                               {"error", cola.error}};
      std::ofstream(cdir / "ola.json") << json_dump_safe(cola_j) << "\n";
      control_turns.push_back({{"turn", turn},
                               {"jobs_before", jobs_run},
                               {"ok", cola.ok},
                               {"do", do_name},
                               {"consulta", cola.consulta},
                               {"consultas", cola.consultas},
                               {"ids", cola.ids},
                               {"hacia", cola.hacia},
                               {"why", cola.why},
                               {"error", cola.error}});
      std::cerr << "wave-explore: control turn #" << turn << " do=" << do_name
                << " ok=" << (cola.ok ? "yes" : "no") << " jobs=" << jobs_run;
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Explorar) {
        std::cerr << " n=" << cola.consultas.size();
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Bosquejar) {
        std::cerr << " n=" << cola.hacia.size();
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Agujas) {
        std::cerr << " n=" << cola.hacia.size();
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Ampliar) {
        std::cerr << " ids=" << cola.ids.size();
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Zoom) {
        std::cerr << " ids=" << cola.ids.size();
      }
      if (cola.ok && cola.do_kind == tuide::WaveControlDo::Plan) {
        std::cerr << " fases=" << cola.plan.fases.size();
      }
      if (!cola.ok) {
        std::cerr << " err=" << cola.error;
      }
      std::cerr << "\n";
      if (!cola.ok) {
        control_last_reject = std::string(tuide::wave_control_do_name(cola.do_kind)) + " → " +
                              cola.error;
        const bool retry =
            cola.error.find("ya está abierta") != std::string::npos ||
            cola.error.find("máx 6") != std::string::npos ||
            cola.error.find("plan 2") != std::string::npos ||
            cola.error.find("plan con fases") != std::string::npos ||
            cola.error.find("need debe") != std::string::npos ||
            cola.error.find("sin locator previo") != std::string::npos ||
            cola.error.find("id de fase repetido") != std::string::npos ||
            cola.error.find("do no legal") != std::string::npos ||
            cola.error.find("pasar exige") != std::string::npos ||
            cola.error.find("bosquejar") != std::string::npos ||
            cola.error.find("agujas") != std::string::npos ||
            cola.error.find("zoom") != std::string::npos ||
            cola.error.find("hacia es un objeto") != std::string::npos ||
            cola.error.find("evita") != std::string::npos ||
            cola.error.find("consulta sin objeto") != std::string::npos ||
            cola.error.find("polo anterior") != std::string::npos ||
            cola.error.find("locator sin consulta") != std::string::npos ||
            cola.error.find("puente sin consulta") != std::string::npos ||
            cola.error.find("seguir sin consulta") != std::string::npos ||
            cola.error.find("sin objeto JSON") != std::string::npos ||
            cola.error.find("JSON control inválido") != std::string::npos ||
            (cola.error.find("1–3 palabras") != std::string::npos &&
             cola.error.find("evita") == std::string::npos &&
             control_cerca_retries < 2) ||
            (cola.error.find("recap del ancla") != std::string::npos &&
             control_recap_retries < 2) ||
            (cola.error.find("desplazamiento") != std::string::npos &&
             control_recap_retries < 2) ||
            (cola.error.find("no pases el ancla") != std::string::npos &&
             control_recap_retries < 2) ||
            cola.error.find("plantilla") != std::string::npos ||
            cola.error.find("no encontró el mapeo") != std::string::npos ||
            cola.error.find("ya se contestó") != std::string::npos ||
            (cola.error.find("cierre vacío") != std::string::npos &&
             control_cerrar_retries < 2) ||
            (cola.error.find("rama") != std::string::npos && control_rama_retries < 2);
        if (retry) {
          if (cola.error.find("rama") != std::string::npos) {
            ++control_rama_retries;
            control_nudge = tuide::wave_control_rama_nudge(cola.consulta);
          } else if (cola.error.find("plantilla") != std::string::npos) {
            ++control_plantilla_retries;
            const bool misma =
                !control_last_plantilla.empty() && cola.consulta == control_last_plantilla;
            control_last_plantilla = cola.consulta;
            control_nudge = tuide::wave_control_plantilla_nudge(
                cola.consulta, misma || control_plantilla_retries >= 2);
          } else if (cola.error.find("no encontró el mapeo") != std::string::npos ||
                     cola.error.find("ya se contestó") != std::string::npos) {
            ++control_replay_retries;
            const std::string prev =
                prev_consultas.empty() ? std::string{} : prev_consultas.back();
            control_nudge = tuide::wave_control_fallo_replay_nudge(
                prev, cola.consulta, control_induce, control_replay_retries >= 2);
          } else if (cola.error.find("polo anterior") != std::string::npos) {
            ++control_cerrar_retries;
            control_nudge =
                "El último Cerrado negó el mapeo. Cierra ESE mapeo (hueco o sin camino), "
                "no selles un polo que sí se leyó.";
          } else if (cola.error.find("cierre vacío") != std::string::npos) {
            ++control_cerrar_retries;
            control_nudge = tuide::wave_control_cerrar_nudge();
          } else if (cola.error.find("ya está abierta") != std::string::npos ||
                     cola.error.find("máx 6") != std::string::npos) {
            ++control_open_retries;
            if (control_open_retries >= 2) {
              control_ampliar_lock = true;
            }
            control_nudge =
                "Esas fichas ya están en inspect. PROHIBIDO volver a ampliarlas. "
                "explorar (una caza) o plan (cazas independientes).";
          } else if (cola.error.find("evita") != std::string::npos) {
            control_nudge = tuide::wave_control_evita_nudge();
          } else if (cola.error.find("hacia es un objeto") != std::string::npos ||
                     cola.error.find("1–3 palabras") != std::string::npos) {
            ++control_cerca_retries;
            control_nudge =
                "hacia es 1–3 palabras, no el texto de un port. Si es largo se recorta o se omite; "
                "la consulta sí se lanza. Un objeto (entrada, parada), no una frase.";
          } else if (cola.error.find("consulta sin objeto") != std::string::npos) {
            control_nudge = tuide::wave_control_sin_objeto_nudge();
          } else if (cola.error.find("recap del ancla") != std::string::npos ||
                     cola.error.find("desplazamiento") != std::string::npos ||
                     cola.error.find("no pases el ancla") != std::string::npos) {
            ++control_recap_retries;
            control_nudge =
                "Recap es pegar el ancla. Nombrar un disparo de lo visto (un objeto) no es recap, "
                "aunque el ancla use las mismas palabras. Si el ancla enumera, un hijo un disparo. "
                "hacia es opcional.";
          } else if (cola.error.find("sin objeto JSON") != std::string::npos ||
                     cola.error.find("JSON control inválido") != std::string::npos) {
            control_nudge = "JSON. Primer carácter `{`. Sin prosa.";
          } else if (cola.error.find("zoom") != std::string::npos) {
            control_nudge =
                "zoom pide 1–3 ids del plano (barrio o stem). No M*, no paths. "
                "PROHIBIDO repetir un zoom ya abierto.";
          } else if (cola.error.find("do no legal") != std::string::npos ||
                     cola.error.find("pasar exige") != std::string::npos) {
            if (cola.do_kind == tuide::WaveControlDo::Explorar &&
                !tuide::wave_control_do_allowed(legal_now, tuide::WaveControlDo::Explorar) &&
                tuide::wave_control_do_allowed(legal_now, tuide::WaveControlDo::Plan)) {
              control_nudge =
                  "explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) "
                  "o cierra citando el mecanismo leído o la refutación. "
                  "PROHIBIDO 'el ancla queda contestada'.";
            } else if ((cola.do_kind == tuide::WaveControlDo::Explorar ||
                        cola.do_kind == tuide::WaveControlDo::Plan ||
                        cola.do_kind == tuide::WaveControlDo::NoPasar ||
                        cola.do_kind == tuide::WaveControlDo::Pasar) &&
                       !tuide::wave_control_do_allowed(legal_now, tuide::WaveControlDo::Plan) &&
                       tuide::wave_control_do_allowed(legal_now, tuide::WaveControlDo::Cerrar)) {
              control_nudge =
                  "exploradores agotados. Cierra citando el mecanismo leído o la refutación. "
                  "PROHIBIDO 'el ancla queda contestada'.";
            } else if ((!tuide::kWaveControlCatalogLive &&
                 (cola.do_kind == tuide::WaveControlDo::Agujas ||
                  cola.do_kind == tuide::WaveControlDo::Zoom)) ||
                (!tuide::kWaveControlBosquejoLive &&
                 cola.do_kind == tuide::WaveControlDo::Bosquejar)) {
              control_nudge =
                  "bosquejo y censo están desconectados. amplia M* del atlas o explora un objeto.";
            } else {
              control_nudge = cola.error + " " + tuide::wave_control_legal_markdown(legal_now);
            }
          } else if (cola.error.find("agujas") != std::string::npos) {
            if (cola.error.find("máx") != std::string::npos ||
                cola.error.find("sin zona nueva") != std::string::npos) {
              control_nudge =
                  "ya hay luz acumulada. zoom a un barrio. PROHIBIDO repetir las mismas zonas.";
            } else {
              control_nudge =
                  "agujas son clases de sitio (event, mouse, ai, file) que cubran las zonas del "
                  "ancla, no idents del prompt. El calor se acumula.";
            }
          } else if (cola.error.find("bosquejar") != std::string::npos) {
            if (cola.error.find("máx") != std::string::npos) {
              control_nudge =
                  "bosquejar agotado (máx 2). PROHIBIDO otro bosquejar. "
                  "Siguiente: explorar un fenómeno, ampliar M*, plan, o cerrar el claim.";
            } else {
              control_nudge =
                  "bosquejar pide 2–8 olores de zona (1–3 palabras): clase de sitio, no ids, "
                  "no copiar el ancla.";
            }
          } else if (cola.error.find("1–3 palabras") != std::string::npos) {
            control_nudge =
                "hacia/conceptos: 1–3 palabras, no frase ni id. Si es largo se recorta o se omite.";
          } else if (cola.error.find("locator sin consulta") != std::string::npos ||
                     cola.error.find("puente sin consulta") != std::string::npos ||
                     cola.error.find("seguir sin consulta") != std::string::npos) {
            control_nudge =
                "Cada locator, puente y seguir trae consulta tuya. hacia es cerca, no el briefing. "
                "El runtime no redacta la consulta.";
          } else if (cola.error.find("plan 2") != std::string::npos ||
              cola.error.find("plan con fases") != std::string::npos ||
              cola.error.find("need debe") != std::string::npos ||
              cola.error.find("sin locator previo") != std::string::npos ||
              cola.error.find("id de fase repetido") != std::string::npos) {
            if (cola.error.find("need debe") != std::string::npos ||
                cola.error.find("sin locator previo") != std::string::npos ||
                cola.error.find("id de fase repetido") != std::string::npos) {
              control_nudge =
                  "need nombra ids de fases que ya salieron en ESTE plan, no del anterior. "
                  "Reordena, o suelta el plan y cierra o explora un objeto.";
            } else {
              control_nudge = tuide::wave_control_plan_nudge();
            }
          } else {
            control_nudge =
                "Esas fichas ya están en inspect. PROHIBIDO volver a ampliarlas. "
                "explorar (una caza) o plan (cazas independientes).";
          }
          continue;
        }
        control_why = cola.error;
        break;
      }
      control_last_reject.clear();
      if (cola.do_kind == tuide::WaveControlDo::Cerrar) {
        control_cerró = true;
        control_why = cola.why;
        break;
      }
      if (cola.do_kind == tuide::WaveControlDo::Plan) {
        std::string perr;
        if (!tuide::wave_control_plan_commit(&control_plan, cola, &perr)) {
          control_nudge = perr.empty() ? "El plan no se pudo congelar." : perr;
          continue;
        }
        control_job_cap = tuide::wave_control_job_cap(control_plan);
        if (jobs_run >= control_job_cap) {
          control_job_cap = std::max(control_job_cap,
                                     tuide::kWaveControlMaxJobs + tuide::kWaveControlLatePlanJobs);
        }
        write_plan_files(cola.why);
        if (!control_run_jobs) {
          control_why = "plan: " + std::to_string(control_plan.fases.size()) +
                        " fases (exploradores no lanzados)";
          std::cerr << "wave-explore: " << control_why << "\n";
          break;
        }
        if (!launch_current_phase()) {
          break;
        }
        continue;
      }
      if (cola.do_kind == tuide::WaveControlDo::Pasar) {
        std::string perr;
        if (!tuide::wave_control_plan_pasar(&control_plan, last_visto, &perr)) {
          control_nudge = perr.empty() ? "pasar no vale." : perr;
          continue;
        }
        write_plan_files(cola.why);
        last_visto.clear();
        const auto next = tuide::wave_control_launch_spec(control_plan);
        if (next.ok) {
          if (!control_run_jobs) {
            control_why = "plan: siguiente fase sin lanzar";
            break;
          }
          if (!launch_current_phase()) {
            break;
          }
        }
        continue;
      }
      if (cola.do_kind == tuide::WaveControlDo::NoPasar) {
        std::string perr;
        if (!tuide::wave_control_plan_no_pasar(&control_plan, &perr)) {
          control_nudge = perr.empty() ? "no_pasar no vale." : perr;
          continue;
        }
        write_plan_files(cola.why);
        last_visto.clear();
        if (tuide::wave_control_plan_current(control_plan) >= 0) {
          if (!control_run_jobs) {
            control_why = "plan: siguiente fase sin lanzar";
            break;
          }
          if (!launch_current_phase()) {
            break;
          }
        }
        continue;
      }
      if (cola.do_kind == tuide::WaveControlDo::Revisar) {
        std::string perr;
        if (last_hunt_failed() &&
            !tuide::wave_control_consulta_replay_ok(cola.consulta, prev_consultas, true, &perr,
                                                   failed_consultas)) {
          ++control_replay_retries;
          control_nudge = tuide::wave_control_fallo_replay_nudge(
              prev_consultas.empty() ? std::string{} : prev_consultas.back(), cola.consulta,
              control_induce, control_replay_retries >= 2);
          continue;
        }
        if (!tuide::wave_control_plan_revisar(&control_plan, cola.consulta, cola.hacia, &perr,
                                             cola.evita)) {
          control_nudge = perr.empty() ? "revisar no vale." : perr;
          continue;
        }
        write_plan_files(cola.why);
        last_visto.clear();
        if (!control_run_jobs) {
          control_why = "plan: fase revisada sin lanzar";
          break;
        }
        if (!launch_current_phase()) {
          break;
        }
        continue;
      }
      if (cola.do_kind == tuide::WaveControlDo::Ampliar) {
        for (const auto& id : cola.ids) {
          control_ampliar_ids.push_back(id);
        }
        std::string opened = tuide::registry_causal_pilot_opened_pack(
            cards_payload, control_ampliar_ids, user_consulta);
        const auto filtered =
            tuide::registry_causal_payload_filter_zones(cards_payload, control_ampliar_ids);
        const std::string inspect =
            tuide::registry_causal_pack_markdown(filtered, tuide::GraphViewLevel::Inspect);
        if (!inspect.empty()) {
          opened += "\n";
          opened += inspect;
        }
        std::ofstream(cdir / "opened.md") << opened << "\n";
        control_inspect = tuide::wave_control_opened_brief(opened);
        std::ofstream(cdir / "inspect.md") << control_inspect << "\n";
        control_atlas = control_atlas_from(control_ampliar_ids);
        std::ofstream(cdir / "rest.md") << control_atlas << "\n";
        control_amplió = true;
        ++control_ampliar_turns;
        continue;
      }
      if (cola.do_kind == tuide::WaveControlDo::Zoom) {
        for (const auto& id : cola.ids) {
          control_zoom_ids.push_back(id);
        }
        refresh_orientation();
        std::ofstream(cdir / "zoom.md") << control_zoom_md << "\n";
        continue;
      }
      if (cola.do_kind == tuide::WaveControlDo::Agujas) {
        const int added = tuide::wave_control_agujas_merge(&control_agujas, cola.hacia);
        ++control_agujas_n;
        refresh_orientation();
        std::ofstream(cdir / "agujas.md") << control_plano << "\n";
        std::ofstream(output_root / "agujas.md") << control_plano << "\n";
        std::cerr << "wave-explore: agujas #" << control_agujas_n << " +" << added;
        for (const auto& a : control_agujas) {
          std::cerr << " " << a;
        }
        std::cerr << "\n";
        continue;
      }
      if (cola.do_kind == tuide::WaveControlDo::Bosquejar) {
        tuide::WaveControlBosquejo foto;
        std::string berr;
        tuide::RegistryEmbedFn cembed;
        if (embed_backend.ready() || ensure_embed_backend(root, &embed_backend, &embed_err)) {
          cembed = [&](bool is_query, const std::string& text, std::vector<float>* outv) {
            return is_query ? embed_backend.embed_query(text, outv, &embed_err)
                            : embed_backend.embed_passage(text, outv, &embed_err);
          };
        }
        if (!tuide::wave_control_bosquejo_from_registry(&reg, cembed, cola.hacia, &foto, &berr)) {
          control_nudge = berr.empty()
                              ? "No pude pintar el bosquejo. Prueba otros conceptos."
                              : berr;
          continue;
        }
        control_bosquejo = tuide::wave_control_bosquejo_markdown(foto);
        ++control_bosquejos;
        std::ofstream(cdir / "bosquejo.md") << control_bosquejo << "\n";
        std::ofstream(output_root / "bosquejo.md") << control_bosquejo << "\n";
        std::cerr << "wave-explore: bosquejo #" << control_bosquejos;
        if (!foto.nota.empty()) {
          std::cerr << " " << foto.nota;
        }
        std::cerr << "\n";
        continue;
      }
      if (cola.do_kind == tuide::WaveControlDo::Explorar && control_plan.committed) {
        control_plan = tuide::WaveControlPlan{};
        write_plan_files(cola.why);
      }
      if (!control_run_jobs) {
        nlohmann::json plan = {{"consultas", cola.consultas},
                               {"why", cola.why},
                               {"amplió", control_amplió},
                               {"ids", control_ampliar_ids}};
        std::ofstream(output_root / "plan.json") << json_dump_safe(plan) << "\n";
        std::ostringstream plan_md;
        plan_md << "Plan (exploradores no lanzados)\n";
        for (const auto& c : cola.consultas) {
          plan_md << "- " << c << "\n";
        }
        plan_md << "why: " << cola.why << "\n";
        std::ofstream(output_root / "plan.md") << plan_md.str();
        control_why = "plan: " + std::to_string(cola.consultas.size()) +
                      " consulta(s) (exploradores no lanzados)";
        std::cerr << "wave-explore: " << control_why << "\n";
        break;
      }
      bool launched = false;
      for (const auto& c : cola.consultas) {
        if (jobs_run >= tuide::kWaveControlMaxJobs) {
          control_why = "presupuesto de trabajos agotado";
          break;
        }
        tuide::WaveControlLaunch spec;
        spec.consulta = c;
        spec.hacia = cola.hacia;
        spec.evita = cola.evita;
        spec.ok = true;
        run_explorer_job(c, spec);
        launched = true;
      }
      if (!launched) {
        if (control_why.empty()) {
          control_why = "control: sin explorador";
        }
        break;
      }
    }
    }
    live = &state;
    control_job_n = 0;
    state.cierre = control_why;
    state.done = jobs_run > 0 || control_why.rfind("plan:", 0) == 0;
    std::ofstream(output_root / "jobs.md") << jobs_md;
    if (!circuit_md.empty()) {
      std::ofstream(output_root / "jobs.md", std::ios::app) << "\n" << circuit_md;
      if (circuit_md.back() != '\n') {
        std::ofstream(output_root / "jobs.md", std::ios::app) << "\n";
      }
    }
    nlohmann::json control_out = {{"consulta", user_consulta},
                                  {"jobs", jobs},
                                  {"turns", control_turns},
                                  {"cerró", control_cerró},
                                  {"why", control_why},
                                  {"jobs_run", jobs_run},
                                  {"piloto", script_consultas.empty()},
                                  {"script", script_arg},
                                  {"plan", tuide::wave_control_plan_to_json(control_plan)},
                                  {"amplió", control_amplió},
                                  {"bosquejos", control_bosquejos},
                                  {"agujas", control_agujas},
                                  {"zooms", control_zoom_ids},
                                  {"induce", tuide::wave_control_induce_name(control_induce)},
                                  {"circuit", !circuit_md.empty()}};
    std::ofstream(output_root / "control.json") << json_dump_safe(control_out) << "\n";
    tuide::registry_close(&reg);
    std::ofstream(output_root / "log.json") << json_dump_safe(log) << "\n";
    nlohmann::json metrics = {{"control", true},
                              {"piloto", script_consultas.empty()},
                              {"induce", tuide::wave_control_induce_name(control_induce)},
                              {"jobs_run", jobs_run},
                              {"cerró", control_cerró},
                              {"why", control_why},
                              {"think",
                               close_audit ? "close"
                                           : (think_override ? tuide::l2_think_level_name(think_level)
                                                             : "hybrid")},
                              {"think_escalates", escalate_n},
                              {"user_chars_max", max_user_chars}};
    std::ofstream(output_root / "metrics.json") << json_dump_safe(metrics) << "\n";
    std::ofstream(output_root / "notebook.md") << jobs_md;
    std::ofstream(output_root / "state.json") << json_dump_safe(tuide::wave_state_to_json(state))
                                             << "\n";
    std::cout << json_dump_safe(control_out) << "\n";
    return (jobs_run > 0 || control_why.rfind("plan:", 0) == 0) ? 0 : 1;
  }
  run_waves(state, output_root, max_waves);

  tuide::registry_close(&reg);
  std::ofstream(output_root / "log.json") << json_dump_safe(log) << "\n";
  nlohmann::json metrics = {{"proposes", state.propose_n},
                            {"applies", state.wave_n},
                            {"cerró", state.done},
                            {"circuit_complete", tuide::wave_circuit_complete(state)},
                            {"circuit_on", state.circuit_on},
                            {"circuit_off", state.circuit_off},
                            {"circuit_callers_on", state.circuit_callers_on},
                            {"first_circuit_propose", first_circuit_propose},
                            {"waves_tras_circuito",
                             first_circuit_propose == 0
                                 ? 0
                                 : std::max(0, state.propose_n - first_circuit_propose)},
                            {"think",
                             close_audit ? "close"
                                         : (think_override ? tuide::l2_think_level_name(think_level)
                                                           : "hybrid")},
                            {"think_escalates", escalate_n},
                            {"close_audit", close_audit_done},
                            {"user_chars_max", max_user_chars},
                            {"user_chars_last",
                             log.empty() ? 0 : log.back().value("user_chars", 0)}};
  std::ofstream(output_root / "metrics.json") << json_dump_safe(metrics) << "\n";
  std::ofstream(output_root / "notebook.md") << tuide::wave_notebook_markdown(state);
  std::ofstream(output_root / "state.json") << json_dump_safe(tuide::wave_state_to_json(state))
                                           << "\n";
  {
    const auto packj = tuide::wave_pack_to_json(state);
    std::ofstream(output_root / "pack.json") << json_dump_safe(packj) << "\n";
    std::ostringstream pack_md;
    pack_md << tuide::wave_pack_markdown(state);
    pack_md << "\n## Código\n";
    const auto handoff = tuide::wave_pack_handoff(state);
    constexpr std::size_t kPackCap = 100000;
    for (const auto& loc : handoff.visto) {
      if (pack_md.str().size() >= kPackCap) {
        pack_md << "\n…[pack truncado]…\n";
        break;
      }
      std::string text;
      std::string perr;
      pack_md << "\n### `" << loc << "`\n\n";
      if (ops.peek_code(loc, &text, &perr) && !text.empty()) {
        pack_md << "```cpp\n" << text;
        if (!text.empty() && text.back() != '\n') {
          pack_md << "\n";
        }
        pack_md << "```\n";
      } else {
        pack_md << "(no se pudo leer";
        if (!perr.empty()) {
          pack_md << ": " << perr;
        }
        pack_md << ")\n";
      }
    }
    std::ofstream(output_root / "pack.md") << pack_md.str();
  }
  if (state.done) {
    std::ostringstream cierre;
    cierre << state.cierre << "\n\n" << tuide::wave_pack_markdown(state);
    std::ofstream(output_root / "cierre.md") << cierre.str();
  }
  std::cout << json_dump_safe(tuide::wave_state_to_json(state)) << "\n";
  return state.done ? 0 : 1;
}

int run_admin_run(const std::string& workspace_root, int argc, char** argv) {
  std::string out_dir;
  std::string prompt;
  std::string script_path;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if ((a == "--out" || a == "-o") && i + 1 < argc) {
      out_dir = argv[++i];
    } else if ((a == "--prompt" || a == "-p") && i + 1 < argc) {
      prompt = argv[++i];
    } else if (a == "--script" && i + 1 < argc) {
      script_path = argv[++i];
    } else if (a == "--help" || a == "-h") {
      std::cerr << "admin-run --out DIR --prompt TEXT [--script FILE]\n"
                << "  Script: one admin_v1 JSON per line (Cursor/T0 stub). Sin --script usa L2 brain.\n"
                << "  No modifica wave-explore ni .tuide/ai/l2_wave/.\n";
      return 2;
    }
  }
  if (out_dir.empty() || prompt.empty()) {
    std::cerr << "admin-run: requiere --out DIR y --prompt TEXT\n";
    return 2;
  }
  std::error_code ec;
  fs::create_directories(out_dir, ec);

  // Persist under --out (isolated from workspace sense artifacts).
  const std::string admin_root = out_dir;
  tuide::AdminState st;
  st.consulta = prompt;

  tuide::AdminOps ops;
  ops.run_build = [](const tuide::AdminSpawn& s) {
    tuide::AdminJobResult r;
    r.ok = true;
    r.summary = "build stub ok arg=" + s.arg;
    r.facts.push_back("build:" + s.arg);
    return r;
  };
  ops.run_shell = [workspace_root](const tuide::AdminSpawn& s) {
    return tuide::admin_run_shell_safe(s.arg, workspace_root);
  };
  ops.run_search = [workspace_root](const tuide::AdminSpawn& s) {
    return tuide::admin_run_search_rg(s.arg, workspace_root);
  };
  ops.run_read = [workspace_root](const tuide::AdminSpawn& s) {
    return tuide::admin_run_read_file(s.arg, workspace_root);
  };
  ops.run_diagnostics = [](const tuide::AdminSpawn& s) {
    return tuide::admin_run_diagnostics_stub(s);
  };
  ops.run_test = [](const tuide::AdminSpawn& s) { return tuide::admin_run_test_stub(s); };
  ops.run_git = [](const tuide::AdminSpawn& s) {
    tuide::AdminJobResult r;
    r.ok = true;
    r.summary = "git stub " + s.arg;
    r.facts.push_back("git:" + s.arg);
    return r;
  };
  ops.run_edit = [workspace_root, &st](const tuide::AdminSpawn& s) {
    return tuide::admin_run_edit_file(s, st, workspace_root);
  };
  ops.run_web = [](const tuide::AdminSpawn& s) { return tuide::admin_run_web_search(s.arg); };
  ops.run_web_fetch = [&st](const tuide::AdminSpawn& s) {
    return tuide::admin_run_web_fetch(s.arg, st);
  };

  std::unique_ptr<tuide::L2Brain> brain;
  if (!script_path.empty()) {
    std::ifstream in(script_path);
    if (!in) {
      std::cerr << "admin-run: no se pudo leer " << script_path << "\n";
      return 1;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line[0] != '#') {
        lines.push_back(line);
      }
    }
    brain = std::make_unique<tuide::AdminScriptedBrain>(std::move(lines));
  } else {
    const AiSettings settings = load_ai_settings(workspace_root);
    std::string err;
    auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
    brain = ready_l2_brain(settings, progress, &err);
    if (!brain) {
      std::cerr << "admin-run: " << err << " (o usa --script FILE)\n";
      return 1;
    }
  }

  tuide::AdminLoopOpts opts;
  opts.workspace_root = admin_root;
  opts.on_line = [](const std::string& line) { std::cout << line << std::endl; };
  if (!script_path.empty()) {
    // Scripted: stub explore (guion no alimenta hijo LLM).
    ops.run_explore = [](const tuide::AdminSpawn& s) { return tuide::admin_explore_stub(s); };
  } else {
    ops.run_explore = [&brain, workspace_root, &opts](const tuide::AdminSpawn& s) {
      return tuide::admin_run_explore_lite(s, *brain, workspace_root, opts);
    };
    const AiSettings settings = load_ai_settings(workspace_root);
    opts.settings = settings.level2;
  }
  const auto res = tuide::run_admin_loop(&st, *brain, ops, opts);
  const auto state_json = tuide::admin_state_to_json(st);
  {
    std::ofstream out(fs::path(out_dir) / "admin.json");
    out << state_json.dump(2) << '\n';
  }
  std::cout << state_json.dump(2) << '\n';
  return res.ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string cmd = argv[1];
  const std::string root = [] {
    const char* env = std::getenv("TUIDE_ROOT");
    if (env && *env) {
      return std::string(env);
    }
    return fs::current_path().string();
  }();

  ToolRegistry tools;
  register_read_tools(&tools, root);
  Level2Session session(tuide::Level2SessionDeps{&tools, {}, {}});

  if (cmd == "status") {
    std::cout << session.status_text(root);
    return 0;
  }
  if (cmd == "trail-probe") {
    return run_trail_probe(&tools, root, argc, argv);
  }
  if (cmd == "slice-probe") {
    return run_slice_probe(&tools, root, argc, argv);
  }
  if (cmd == "registry-ingest") {
    return run_registry_ingest(root, argc, argv);
  }
  if (cmd == "registry-refresh") {
    return run_registry_refresh(root, argc, argv);
  }
  if (cmd == "registry-gc") {
    return run_registry_gc(root, argc, argv);
  }
  if (cmd == "registry-stats") {
    return run_registry_stats(root, argc, argv);
  }
  if (cmd == "registry-get") {
    return run_registry_get(root, argc, argv);
  }
  if (cmd == "registry-neighbors") {
    return run_registry_neighbors(root, argc, argv);
  }
  if (cmd == "registry-path") {
    return run_registry_path(root, argc, argv);
  }
  if (cmd == "registry-code") {
    return run_registry_code(root, argc, argv);
  }
  if (cmd == "registry-files" || cmd == "registry-pending-inventory") {
    return run_registry_files(root, argc, argv);
  }
  if (cmd == "registry-embed") {
    return run_registry_embed(root, argc, argv);
  }
  if (cmd == "registry-query") {
    return run_registry_query(root, argc, argv);
  }
  if (cmd == "entityness-probe") {
    return run_entityness_probe(root, argc, argv);
  }
  if (cmd == "wave-bosquejo") {
    return run_wave_bosquejo(root, argc, argv);
  }
  if (cmd == "catalog-build" || cmd == "catalog-plano" || cmd == "catalog-zoom" ||
      cmd == "catalog-search") {
    return run_catalog(root, argc, argv);
  }
  if (cmd == "atlas-survey") {
    return run_atlas_survey(root, argc, argv);
  }
  if (cmd == "wave-explore") {
    return run_wave_explore(root, argc, argv);
  }
  if (cmd == "admin-run" || cmd == "admin-turn") {
    return run_admin_run(root, argc, argv);
  }
  if (cmd == "wave-control-shot") {
    return run_wave_control_shot(root, argc, argv);
  }
  if (cmd == "worker-probe") {
    return run_worker_probe(root, argc, argv);
  }
  if (cmd == "zone-judge-shot") {
    return run_zone_judge_shot(root, argc, argv);
  }
  if (cmd == "zone-judge-battery") {
    return run_zone_judge_battery(root, argc, argv);
  }
  if (cmd == "trail-judge-shot") {
    return run_trail_judge_shot(&tools, root, argc, argv);
  }
  if (cmd == "dataflow-probe") {
    return run_dataflow_probe(&tools, root, argc, argv);
  }
  if (cmd == "effect-summary-probe") {
    return run_effect_summary_probe(root, argc, argv);
  }
  if (cmd == "card-embed-bench") {
    return run_card_embed_bench(root, argc, argv);
  }
  if (cmd == "a0-sniff-shot") {
    return run_a0_sniff_shot(session, root, argc, argv);
  }
  if (cmd == "a0-sniff-judge-shot") {
    return run_a0_sniff_judge_shot(session, root, argc, argv);
  }
  if (cmd == "a0-first-judge-shot") {
    return run_a0_first_judge_shot(session, root, argc, argv);
  }
  if (cmd == "a0-tranche-rank-shot") {
    return run_a0_tranche_rank_shot(session, root, argc, argv);
  }
  if (cmd == "bootstrap") {
    Level2BootstrapOpts opts;
    opts.workspace_root = root;
    // Parse optional flags before positional query.
    int positional = 2;
    for (int i = 2; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--problem-frame-json" && i + 1 < argc) {
        ++i;
        opts.problem_frame_json = read_file(argv[i]);
        positional = i + 1;
      } else if (a == "--distilled-intent-json" && i + 1 < argc) {
        ++i;
        opts.distilled_intent_json = read_file(argv[i]);
        positional = i + 1;
      } else if (a == "--seeds" && i + 1 < argc) {
        // Accept a JSON file path or a comma-separated list of stems.
        ++i;
        const std::string seeds_arg = argv[i];
        if (!seeds_arg.empty() && seeds_arg.front() == '[') {
          // Inline JSON array.
          try {
            // Simple parse: extract quoted strings between [ ].
            std::string s = seeds_arg;
            std::size_t p = 0;
            while ((p = s.find('"', p)) != std::string::npos) {
              ++p;
              const auto e = s.find('"', p);
              if (e == std::string::npos) { break; }
              const std::string tok = s.substr(p, e - p);
              if (!tok.empty()) { opts.seeds.push_back(tok); }
              p = e + 1;
            }
          } catch (...) {}
        } else if (!seeds_arg.empty() && (seeds_arg[0] == '/' || seeds_arg[0] == '.')) {
          // File path containing a JSON array.
          const std::string raw = read_file(seeds_arg);
          std::size_t p = 0;
          while ((p = raw.find('"', p)) != std::string::npos) {
            ++p;
            const auto e = raw.find('"', p);
            if (e == std::string::npos) { break; }
            const std::string tok = raw.substr(p, e - p);
            if (!tok.empty()) { opts.seeds.push_back(tok); }
            p = e + 1;
          }
        } else {
          // Comma-separated stems.
          std::istringstream ss(seeds_arg);
          std::string tok;
          while (std::getline(ss, tok, ',')) {
            while (!tok.empty() && tok.front() == ' ') { tok.erase(0, 1); }
            while (!tok.empty() && tok.back() == ' ') { tok.pop_back(); }
            if (!tok.empty()) { opts.seeds.push_back(tok); }
          }
        }
        positional = i + 1;
      } else {
        positional = i;
        break;
      }
    }
    opts.query = positional < argc ? argv[positional] : std::string{};
    if (opts.query.empty()) {
      const std::string map = read_file(fs::path(root) / ".tuide" / "ai" / "map_last.md");
      const auto qpos = map.find("query:");
      if (qpos != std::string::npos) {
        const auto line_end = map.find('\n', qpos);
        opts.query = trim(map.substr(qpos + 6, line_end == std::string::npos
                                                   ? std::string::npos
                                                   : line_end - (qpos + 6)));
      }
    }
    if (opts.query.empty()) {
      opts.query = "(harness cli bootstrap)";
    }
    opts.instruction =
        "Explore then done next=edit; emit Search/Replace hunks; runtime compiles.";
    std::string err;
    if (!session.bootstrap(opts, &err)) {
      std::cerr << "bootstrap failed: " << err << '\n';
      return 1;
    }
    std::cout << session.status_text(root);
    return 0;
  }
  if (cmd == "tool") {
    if (argc < 3) {
      usage();
      return 2;
    }
    const std::string name = argv[2];
    std::ostringstream arg;
    for (int i = 3; i < argc; ++i) {
      if (i > 3) {
        arg << ' ';
      }
      arg << argv[i];
    }
    const auto tr = session.apply_tool(root, name, arg.str());
    if (!tr.ok) {
      std::cerr << "tool failed: " << tr.error << '\n';
      return 1;
    }
    print_ok_line(tr, session, root, "ok");
    return 0;
  }
  if (cmd == "tools") {
    if (argc < 3) {
      usage();
      return 2;
    }
    const std::string raw = read_file(argv[2]);
    if (raw.empty()) {
      std::cerr << "tools: no se pudo leer " << argv[2] << '\n';
      return 1;
    }
    std::vector<tuide::L2ToolCall> calls;
    try {
      const auto j = nlohmann::json::parse(raw);
      const nlohmann::json* arr = nullptr;
      if (j.is_array()) {
        arr = &j;
      } else if (j.is_object() && j.contains("calls") && j["calls"].is_array()) {
        arr = &j["calls"];
      } else {
        std::cerr << "tools: JSON debe ser array [{name,arg},…] o {\"calls\":[…]}\n";
        return 2;
      }
      for (const auto& c : *arr) {
        tuide::L2ToolCall call;
        call.name = c.value("name", "");
        if (c.contains("arg")) {
          if (c["arg"].is_string()) {
            call.arg = c["arg"].get<std::string>();
          } else {
            call.arg = c["arg"].dump();
          }
        }
        if (!call.name.empty()) {
          calls.push_back(std::move(call));
        }
      }
    } catch (const std::exception& e) {
      std::cerr << "tools: JSON inválido: " << e.what() << '\n';
      return 1;
    }
    if (calls.empty()) {
      std::cerr << "tools: calls vacío\n";
      return 1;
    }
    const auto tr = session.apply_tools(root, calls);
    if (!tr.ok) {
      std::cerr << "tools failed: " << tr.error << '\n';
      return 1;
    }
    print_ok_line(tr, session, root, "ok tools");
    return 0;
  }
  if (cmd == "plan") {
    if (argc < 3) {
      usage();
      return 2;
    }
    std::vector<std::string> targets;
    for (int i = 2; i < argc; ++i) {
      targets.emplace_back(argv[i]);
    }
    const auto tr = session.apply_plan(root, targets, "harness cli plan");
    if (!tr.ok) {
      std::cerr << "plan failed: " << tr.error << '\n';
      return 1;
    }
    print_ok_line(tr, session, root, "ok plan");
    const std::string pack =
        read_file(fs::path(root) / ".tuide" / "ai" / "l2" / "pack.md");
    std::cout << "pack_chars=" << pack.size() << '\n';
    return 0;
  }
  if (cmd == "turn") {
    const auto tr = session.process_request_file(root);
    std::cout << "action=" << tr.action << " turn=" << tr.turn << " phase=" << tr.phase
              << " ok=" << (tr.ok ? 1 : 0) << '\n';
    std::cout << session.status_flags(root) << '\n';
    if (!tr.ok && tr.action != "compile") {
      std::cerr << "turn failed: " << tr.error << '\n';
      return 1;
    }
    return tr.ok ? 0 : 1;
  }
  if (cmd == "done") {
    std::string summary;
    std::string next;
    for (int i = 2; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--edit") {
        next = "edit";
        continue;
      }
      if (a == "--clarify") {
        next = "clarify";
        continue;
      }
      if (!summary.empty()) {
        summary.push_back(' ');
      }
      summary += a;
    }
    const auto tr = session.mark_done(root, summary, next);
    if (!tr.ok) {
      std::cerr << "done failed: " << tr.error << '\n';
      return 1;
    }
    print_ok_line(tr, session, root, "ok done");
    return 0;
  }
  if (cmd == "hunk-try") {
    if (argc < 3) {
      usage();
      return 2;
    }
    const std::string raw = read_file(argv[2]);
    nlohmann::json j;
    try {
      j = nlohmann::json::parse(raw);
    } catch (const std::exception& ex) {
      std::cerr << "{\"ok\":false,\"error\":\"json: " << ex.what() << "\"}\n";
      return 1;
    }
    tuide::SearchReplaceHunk h;
    h.path = j.value("path", "");
    h.search = j.value("search", "");
    h.replace = j.value("replace", "");
    tuide::normalize_hunk_escape_noise(&h);
    std::string text = j.value("text", "");
    if (text.empty() && !h.path.empty()) {
      fs::path p = h.path;
      if (!p.is_absolute()) {
        p = fs::path(root) / h.path;
      }
      text = read_file(p);
    }
    if (text.empty()) {
      std::cout << "{\"ok\":false,\"error\":\"archivo vacío o ausente\"}\n";
      return 1;
    }
    const auto r = tuide::apply_hunk_to_text(text, h);
    nlohmann::json out;
    out["ok"] = r.ok;
    out["error"] = r.error;
    out["path"] = h.path;
    out["start_line"] = r.span.start_line;
    out["byte_begin"] = r.span.byte_begin;
    out["byte_end"] = r.span.byte_end;
    std::cout << out.dump() << '\n';
    return r.ok ? 0 : 1;
  }
  if (cmd == "edit") {
    if (argc < 3) {
      usage();
      return 2;
    }
    const std::string raw = read_file(argv[2]);
    std::string err;
    std::vector<tuide::SearchReplaceHunk> hunks;
    try {
      const auto j = nlohmann::json::parse(raw);
      hunks = tuide::parse_search_replace_json(j, &err);
    } catch (const std::exception& ex) {
      std::cerr << "json: " << ex.what() << '\n';
      return 1;
    }
    if (hunks.empty()) {
      std::cerr << "edit failed: " << err << '\n';
      return 1;
    }
    const auto tr = session.apply_edit(root, hunks);
    std::cout << "edit action=" << tr.action << " phase=" << tr.phase << " ok=" << (tr.ok ? 1 : 0)
              << " — " << tr.summary << '\n';
    std::cout << session.status_flags(root) << '\n';
    return tr.ok ? 0 : 1;
  }
  if (cmd == "compile") {
    const auto tr = session.run_compile(root);
    std::cout << "compile action=" << tr.action << " phase=" << tr.phase
              << " ok=" << (tr.ok ? 1 : 0) << '\n';
    std::cout << session.status_flags(root) << '\n';
    return tr.ok ? 0 : 1;
  }
  if (cmd == "run") {
    const AiSettings settings = load_ai_settings(root);
    std::string err;
    auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
    auto brain_ptr = ready_l2_brain(settings, progress, &err);
    if (!brain_ptr) {
      std::cerr << "run: " << err << '\n';
      return err.find("mode") != std::string::npos ? 2 : 1;
    }
    tuide::L2Brain& brain = *brain_ptr;
    Level2AutonomousLoopOpts opts;
    opts.workspace_root = root;
    opts.settings = settings.level2;
    std::string pack_err;
    if (!load_prompt_pack_into_opts(&opts, &pack_err)) {
      std::cerr << "prompt pack: " << pack_err << '\n';
      return 1;
    }
    if (const char* pack = std::getenv("L2_PROMPT_PACK"); pack != nullptr && pack[0] != '\0') {
      std::cerr << "L2 ▸ prompt_pack=" << pack << '\n';
    } else if (fs::exists("tools/l2_battery/prompt_packs/DEFAULT_PACK")) {
      std::cerr << "L2 ▸ prompt_pack=DEFAULT_PACK\n";
    }
    const auto result = run_level2_autonomous(
        session, brain, opts, [](const std::string& line) { std::cout << line << std::endl; });
    std::cout << "run ok=" << (result.ok ? 1 : 0) << " phase=" << result.phase
              << " steps=" << result.steps << " — "
              << (result.error.empty() ? result.summary : result.error) << '\n';
    std::cout << session.status_flags(root) << '\n';
    return result.ok ? 0 : 1;
  }
  if (cmd == "run-explore") {
    const AiSettings settings = load_ai_settings(root);
    std::string err;
    auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
    auto brain_ptr = ready_l2_brain(settings, progress, &err);
    if (!brain_ptr) {
      std::cerr << "run-explore: " << err << '\n';
      return err.find("mode") != std::string::npos ? 2 : 1;
    }
    tuide::L2Brain& brain = *brain_ptr;
    Level2AutonomousLoopOpts opts;
    opts.workspace_root = root;
    opts.settings = settings.level2;
    opts.stop_at_explore = true;
    if (opts.settings.max_steps > 16) {
      opts.settings.max_steps = 16;
    }
    std::string pack_err;
    if (!load_prompt_pack_into_opts(&opts, &pack_err)) {
      std::cerr << "prompt pack: " << pack_err << '\n';
      return 1;
    }
    const auto result = run_level2_autonomous(
        session, brain, opts, [](const std::string& line) { std::cout << line << std::endl; });
    std::cout << "run-explore ok=" << (result.ok ? 1 : 0) << " phase=" << result.phase
              << " steps=" << result.steps << " — "
              << (result.error.empty() ? result.summary : result.error) << '\n';
    std::cout << session.status_flags(root) << '\n';
    return result.ok ? 0 : 1;
  }
  if (cmd == "run-explore-a") {
    if (!tuide::l2_feat::enabled("L2_EXPLORE_PHASE_A")) {
      std::cerr << "run-explore-a: L2_EXPLORE_PHASE_A off — export L2_FEAT_L2_EXPLORE_PHASE_A=1\n";
      return 2;
    }
    const AiSettings settings = load_ai_settings(root);
    std::string err;
    auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
    auto brain_ptr = ready_l2_brain(settings, progress, &err);
    if (!brain_ptr) {
      std::cerr << "run-explore-a: " << err << '\n';
      return err.find("mode") != std::string::npos ? 2 : 1;
    }
    tuide::L2Brain& brain = *brain_ptr;
    Level2AutonomousLoopOpts opts;
    opts.workspace_root = root;
    opts.settings = settings.level2;
    opts.stop_at_phase_a = true;
    // Phase A budget: peeks/turns caps live in a_state; allow room for soft-retries.
    if (opts.settings.max_steps > 16) {
      opts.settings.max_steps = 16;
    }
    if (opts.settings.max_steps < 16) {
      opts.settings.max_steps = 16;
    }
    std::string pack_err;
    if (!load_prompt_pack_into_opts(&opts, &pack_err)) {
      std::cerr << "prompt pack: " << pack_err << '\n';
      return 1;
    }
    const auto result = run_level2_autonomous(
        session, brain, opts, [](const std::string& line) { std::cout << line << std::endl; });
    std::cout << "run-explore-a ok=" << (result.ok ? 1 : 0) << " phase=" << result.phase
              << " steps=" << result.steps << " — "
              << (result.error.empty() ? result.summary : result.error) << '\n';
    std::cout << session.status_flags(root) << '\n';
    return result.ok ? 0 : 1;
  }
  usage();
  return 2;
}
