#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ai/get_code_of.hpp"
#include "ai/l2_action.hpp"
#include "ai/l2_brain.hpp"
#include "ai/l2_explore_a.hpp"
#include "ai/l2_feat.hpp"
#include "ai/level2_autonomous_loop.hpp"
#include "ai/level2_session.hpp"
#include "ai/search_replace.hpp"
#include "ai/tool_registry.hpp"
#include "ai/ai_types.hpp"

#include <cctype>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using tuide::AiSettings;
using tuide::AiToolResult;
using tuide::GetCodeOfRequest;
using tuide::Level2AutonomousLoopOpts;
using tuide::Level2BootstrapOpts;
using tuide::Level2Session;
using tuide::LocalL2Brain;
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
    const std::string cmd =
        "rg -n --no-heading -e "
        "'^[a-zA-Z_].*\\(.*\\)\\s*\\{?\\s*$|^\\s*(class|struct|namespace|enum)\\s+' " +
        shell_quote(abs.string()) + " | head -n 80";
    std::string body = run_cmd(cmd);
    if (body.empty()) {
      body = "(sin outline rg)\n";
    }
    return AiToolResult{true, "outline: " + trimmed + "\n" + body};
  });

  reg->register_tool("search", "rg", [root](const std::string& arg) {
    const std::string cmd = "rg -n --no-heading -S -g '!build/**' -g '!.git/**' -g '!third_party/**' " +
                            shell_quote(arg) + " " + shell_quote(root) + " | head -n 40";
    std::string body = run_cmd(cmd);
    if (body.empty()) {
      body = "(sin hits)\n";
    }
    return AiToolResult{true, "q=" + arg + "\n" + body};
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
  std::cerr << "Usage: l2_harness_cli bootstrap|tool|tools|plan|turn|done|edit|compile|status|run|run-explore|run-explore-a|trail-probe|trail-judge-shot|dataflow-probe|hunk-try …\n"
            << "  run-explore            // loop until pack completo → edit (no edit/compile)\n"
            << "  run-explore-a          // Phase A only: peeks → a_judge/a_done (no pack B)\n"
            << "  trail-probe SYM […]    // sin LLM: call-stacks TS de símbolos (search+scopes)\n"
            << "  trail-judge-shot [SYM] // 1× LLM: trail mapa L0 → a_trail_judge (caso 17)\n"
            << "  dataflow-probe VAR     // sin LLM: writes/reads/decls vía ripgrep (no LSP)\n"
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
    }
  } catch (...) {
  }
  return s;
}

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
    if (!workspace_root.empty() && h.path.size() > workspace_root.size() &&
        h.path.compare(0, workspace_root.size(), workspace_root) == 0 &&
        (h.path[workspace_root.size()] == '/' || h.path[workspace_root.size()] == '\\')) {
      h.path = h.path.substr(workspace_root.size() + 1);
    }
    if (h.path.rfind("./", 0) == 0) {
      h.path = h.path.substr(2);
    }
    hits.push_back(std::move(h));
  }
  return hits;
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
    std::cerr << "trail-probe: falta SYM (ej. set_busy_spinner)\n";
    return 2;
  }

  int rc = 0;
  for (const auto& sym : symbols) {
    auto search_fn = [&](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
      if (tools == nullptr || !tools->has("search") || symbol.empty()) {
        return {};
      }
      AiToolResult tr = tools->invoke("search", symbol + " path:src/");
      if (!tr.ok || tr.text.find("(sin hits)") != std::string::npos) {
        tr = tools->invoke("search", symbol);
      }
      if (!tr.ok) {
        return {};
      }
      return parse_rg_hits(tr.text, root);
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

// One LLM turn: inject trail pack for a map L0 (e.g. set_busy_spinner) and ask a_trail_judge.
// Success heuristic for case 17: interesting on a stack whose hops mention begin_thinking / AiController.
int run_trail_judge_shot(ToolRegistry* tools, const std::string& root, int argc, char** argv) {
  std::string sym = "set_busy_spinner";
  std::string path_hint = "src/ui/busy_strip.cpp";
  std::string instruction;
  std::string gold_needle = "begin_thinking";
  std::string out_dir;
  bool dry = false;
  bool do_suspect = true;
  std::string gold_var = "agent_busy_";
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
    } else if (a == "-h" || a == "--help") {
      std::cerr
          << "trail-judge-shot [SYM] [--path hint] [--case ID|--instruction TEXT]\n"
             "                 [--gold begin_thinking] [--gold-var agent_busy_]\n"
             "                 [--out DIR] [--dry] [--suspect|--no-suspect]\n"
             "  1) trail → a_trail_judge  2) si interesting → ¿variable crítica?\n"
             "     → dataflow-probe rg de las pistas (reserva para Phase B).\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      sym = a;
    }
  }
  if (instruction.empty()) {
    // Default: case 17
    const fs::path prompts =
        fs::path(root) / "tests/fixtures/stem_boost_battery/prompts_nl_human.json";
    std::ifstream in(prompts);
    if (in) {
      nlohmann::json arr;
      in >> arr;
      for (const auto& c : arr) {
        if (c.value("id", "") == "17_ai_spinner_stuck") {
          instruction = c.value("prompt", "");
          break;
        }
      }
    }
  }
  if (instruction.empty()) {
    instruction =
        "el spinner de la IA se queda infinito aunque el modelo ya terminó; "
        "dónde se controla ese estado de carga";
  }

  auto search_fn = [&](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
    if (tools == nullptr || !tools->has("search") || symbol.empty()) {
      return {};
    }
    AiToolResult tr = tools->invoke("search", symbol + " path:src/");
    if (!tr.ok || tr.text.find("(sin hits)") != std::string::npos) {
      tr = tools->invoke("search", symbol);
    }
    if (!tr.ok) {
      return {};
    }
    return parse_rg_hits(tr.text, root);
  };

  const auto stacks = tuide::a_trail_build_full_stacks(root, sym, path_hint, search_fn,
                                                       tuide::kATrailMaxStacks,
                                                       tuide::kATrailMaxDepth);
  if (stacks.empty()) {
    std::cerr << "trail-judge-shot: sin stacks para `" << sym << "`\n";
    return 1;
  }

  tuide::ATrail tr;
  tr.active = true;
  tr.awaiting_judge = true;
  tr.root_anchor = path_hint.empty() ? sym : (path_hint + ":" + sym);
  tr.focus_anchor = tr.root_anchor;
  tr.focus_symbol = sym;
  tr.root_stem = sym;
  tr.root_why = "hipótesis useful del mapa (target UI / busy)";
  tr.pending_stacks = stacks;
  tuide::ATrailHop root_hop;
  root_hop.symbol = sym;
  root_hop.anchor = tr.root_anchor;
  root_hop.path = path_hint;
  root_hop.summary = "L0 map target";
  tr.trail.push_back(root_hop);

  const std::string trail_md = tuide::a_trail_stacks_markdown(tr);

  // Which stack ids contain the gold needle (for scoring)?
  std::vector<std::string> gold_stack_ids;
  for (const auto& s : stacks) {
    bool hit = false;
    for (const auto& h : s.hops) {
      if (h.symbol.find(gold_needle) != std::string::npos ||
          h.scope_chain.find(gold_needle) != std::string::npos ||
          h.signature.find(gold_needle) != std::string::npos ||
          h.anchor.find(gold_needle) != std::string::npos) {
        hit = true;
        break;
      }
    }
    if (hit) {
      gold_stack_ids.push_back(s.id);
    }
  }

  std::ostringstream user;
  user << "phase=explore_a step=1 workflow=autofix\n\n";
  user << "## Instruction\n" << instruction << "\n\n";
  user << "## Situación (inyección de prueba)\n"
          "El mapa señaló el target `"
       << sym << "` (`" << path_hint
       << "`) como hipótesis **useful** (síntoma: spinner/busy).\n"
          "Eso NO es aún el edit site: el runtime abrió call-stacks entry→…→L0 "
          "con scopes TS + condición de control + snippet.\n"
          "Tu trabajo AHORA: `a_trail_judge`. Marca **interesting** la pila cuyo "
          "caller anidado controla el estado de carga de la IA (no UI genérica / "
          "reindex / outline).\n"
          "Si ves el edit site en un hop, puedes cerrar con `a_done` (≤2 primary).\n\n";
  user << trail_md;

  const std::string system =
      "Eres el Nivel 2 en fase explore_a (localización + trail).\n"
      "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
      "PROHIBIDO action=plan, tool, edit, done next=edit.\n"
      "Objetivo: encontrar el EDIT SITE del síntoma de ## Instruction.\n"
      "Tras useful el runtime muestra call-stacks. Entonces SOLO a_trail_judge.\n"
      "Ejemplo: {\"action\":\"a_trail_judge\",\"verdicts\":["
      "{\"target\":\"S2\",\"verdict\":\"interesting\",\"why\":\"AiController enciende el spinner\"},"
      "{\"target\":\"S1\",\"verdict\":\"reject\",\"why\":\"reindex no es el síntoma IA\"}]}\n"
      "verdict es EXACTAMENTE \"interesting\" o \"reject\" (nunca el literal "
      "\"interesting|reject\").\n"
      "interesting ≤3. Si TODOS reject → L0 se invalida.\n"
      "UI genérica / reindex / search → reject salvo que sea el control de carga de la IA.\n"
      "a_done solo cuando un hop es el edit site (≤2 primary).\n";

  std::cout << "======== trail-judge-shot ========\n";
  std::cout << "L0=" << tr.root_anchor << " stacks=" << stacks.size()
            << " gold_needle=`" << gold_needle << "` gold_in=";
  if (gold_stack_ids.empty()) {
    std::cout << "(ninguna pila — el pack no contiene el gold; abort)\n";
    return 1;
  }
  for (std::size_t i = 0; i < gold_stack_ids.size(); ++i) {
    if (i) {
      std::cout << ",";
    }
    std::cout << gold_stack_ids[i];
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
  if (settings.level2_mode != "local" && settings.level2_mode != "remote") {
    std::cerr << "trail-judge-shot: ai.level2.mode debe ser local|remote (ahora="
              << settings.level2_mode << ")\n";
    return 2;
  }
  // Same as run / run-explore-a: LocalL2Brain (covers local llama-server; remote
  // path still goes through Local when mode mis-set — match existing harness).
  tuide::LocalL2Brain brain;
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  if (!brain.ensure_ready(settings, progress, &err)) {
    std::cerr << "trail-judge-shot: ensure_ready: " << err << "\n";
    return 1;
  }

  tuide::L2BrainRequest breq;
  breq.system_prompt = system;
  breq.user_prompt = user.str();
  breq.phase = "explore_a";
  breq.max_tokens = settings.level2.max_tokens > 0 ? settings.level2.max_tokens : 1024;
  breq.n_ctx = settings.level2.n_ctx > 0 ? settings.level2.n_ctx : 8192;
  breq.temperature = 0.1f;

  std::cout << "L2 ▸ trail-judge-shot pidiendo a_trail_judge (" << brain.name() << ")…\n";
  const auto bres = brain.propose(breq, nullptr);
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
      } else if (v.verdict == tuide::AVerdictKind::Reject) {
        reject_ids.push_back(v.target);
      }
    }
  } else if (action.kind == tuide::L2ActionKind::ADone || action.name == "a_done") {
    // Bonus path: model jumps to a_done naming gold
    for (const auto& loc : action.a_loci) {
      if (loc.anchor.find(gold_needle) != std::string::npos ||
          loc.stem.find("ai_controller") != std::string::npos ||
          loc.anchor.find("AiController") != std::string::npos) {
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
  std::cout << "gold_stacks=[" << join(gold_stack_ids) << "] gold_marked_interesting="
            << (gold_interesting ? 1 : 0) << "\n";
  if (!gold_interesting) {
    std::cout << "FAIL: no marcó interesting la pila con `" << gold_needle << "`\n";
    return 1;
  }
  std::cout << "PASS: el modelo vio el control IA anidado (" << gold_needle << ")\n";

  if (!do_suspect) {
    return 0;
  }

  // --- Phase 2: critical variable? (compact; reserve for B) --------------------
  std::ostringstream focus_md;
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
      "crítica para el síntoma (spinner/busy IA que no se limpia)?\n"
      "Responde EXACTAMENTE:\n"
      "{\"action\":\"a_suspect_vars\",\"vars\":[{\"name\":\"agent_busy_\","
      "\"why\":\"flag que queda true\",\"anchor\":\"src/ai/ai_controller.hpp:"
      "agent_busy_\"}],\"none\":false}\n"
      "o {\"action\":\"a_suspect_vars\",\"vars\":[],\"none\":true}\n"
      "Reglas: máx 2 vars; nombre C++ real (p.ej. agent_busy_ no 'el spinner'); "
      "si no estás seguro → none:true. No inventes paths.\n";

  std::ostringstream user2;
  user2 << "## Instruction\n" << instruction << "\n\n";
  user2 << "## Pilas interesting (contexto)\n" << focus_md.str() << "\n";
  user2 << "Pregunta: en ese código, ¿qué variable/campo controla el estado de carga "
           "de la IA? Emite a_suspect_vars.\n";

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
  const auto bres2 = brain.propose(breq2, nullptr);
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
    if (tools == nullptr || !tools->has("search") || symbol.empty()) {
      return {};
    }
    AiToolResult tr = tools->invoke("search", symbol + " path:src/");
    if (!tr.ok || tr.text.find("(sin hits)") != std::string::npos) {
      tr = tools->invoke("search", symbol);
    }
    if (!tr.ok) {
      return {};
    }
    return parse_rg_hits(tr.text, root);
  };

  auto name_matches_gold = [&](const std::string& n) {
    if (n == gold_var) {
      return true;
    }
    if (n.find(gold_var) != std::string::npos) {
      return true;
    }
    // Soft gold family for case 17
    return n.find("agent_busy") != std::string::npos || n.find("busy_") != std::string::npos ||
           n == "download_busy_" || n.find("cancel_") != std::string::npos;
  };

  bool any_gold_var = false;
  bool any_df_hits = false;
  for (const auto& s : suspects) {
    if (name_matches_gold(s.name)) {
      any_gold_var = true;
    }
    std::string hint;
    if (s.anchor.find(':') != std::string::npos) {
      hint = s.anchor.substr(0, s.anchor.find(':'));
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

  std::cout << "gold_var=`" << gold_var << "` named_gold=" << (any_gold_var ? 1 : 0)
            << " dataflow_hits=" << (any_df_hits ? 1 : 0) << "\n";
  if (any_gold_var && any_df_hits) {
    std::cout << "PASS: pista de variable + data-flow rg extraído (reserva B)\n";
    return 0;
  }
  if (any_df_hits) {
    std::cout << "PARTIAL: data-flow ok pero var ≠ familia gold (`" << gold_var << "`)\n";
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
                   "  Sin LLM/LSP. ripgrep + heurística write/read/decl + snippet.\n"
                   "  Ej.: dataflow-probe agent_busy_ --path src/ai/ai_controller.cpp\n";
      return 2;
    } else if (!a.empty() && a[0] != '-') {
      names.push_back(a);
    }
  }
  if (names.empty()) {
    std::cerr << "dataflow-probe: falta VAR (ej. agent_busy_)\n";
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
  if (cmd == "trail-judge-shot") {
    return run_trail_judge_shot(&tools, root, argc, argv);
  }
  if (cmd == "dataflow-probe") {
    return run_dataflow_probe(&tools, root, argc, argv);
  }
  if (cmd == "bootstrap") {
    Level2BootstrapOpts opts;
    opts.workspace_root = root;
    // Parse optional flags before positional query.
    int positional = 2;
    for (int i = 2; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--seeds" && i + 1 < argc) {
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
    if (settings.level2_mode != "local" && settings.level2_mode != "remote") {
      std::cerr << "run: ai.level2.mode debe ser local|remote (ahora=" << settings.level2_mode
                << ")\n";
      return 2;
    }
    LocalL2Brain brain;
    std::string err;
    auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
    if (!brain.ensure_ready(settings, progress, &err)) {
      std::cerr << "L2 brain ensure_ready: " << err << '\n';
      return 1;
    }
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
    if (settings.level2_mode != "local" && settings.level2_mode != "remote") {
      std::cerr << "run-explore: ai.level2.mode debe ser local|remote (ahora="
                << settings.level2_mode << ")\n";
      return 2;
    }
    LocalL2Brain brain;
    std::string err;
    auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
    if (!brain.ensure_ready(settings, progress, &err)) {
      std::cerr << "L2 brain ensure_ready: " << err << '\n';
      return 1;
    }
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
    if (settings.level2_mode != "local" && settings.level2_mode != "remote") {
      std::cerr << "run-explore-a: ai.level2.mode debe ser local|remote (ahora="
                << settings.level2_mode << ")\n";
      return 2;
    }
    LocalL2Brain brain;
    std::string err;
    auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
    if (!brain.ensure_ready(settings, progress, &err)) {
      std::cerr << "L2 brain ensure_ready: " << err << '\n';
      return 1;
    }
    Level2AutonomousLoopOpts opts;
    opts.workspace_root = root;
    opts.settings = settings.level2;
    opts.stop_at_phase_a = true;
    // Phase A budget: peeks/turns caps live in a_state; keep loop steps modest.
    if (opts.settings.max_steps > 10) {
      opts.settings.max_steps = 10;
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
