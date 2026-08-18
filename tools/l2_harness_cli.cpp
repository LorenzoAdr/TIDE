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
#include "ai/l2_brain.hpp"
#include "ai/level2_autonomous_loop.hpp"
#include "ai/level2_session.hpp"
#include "ai/search_replace.hpp"
#include "ai/tool_registry.hpp"
#include "ai/ai_types.hpp"

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
  std::cerr << "Usage: l2_harness_cli bootstrap|tool|tools|plan|turn|done|edit|compile|status|run|hunk-try …\n"
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
  if (cmd == "bootstrap") {
    Level2BootstrapOpts opts;
    opts.workspace_root = root;
    opts.query = argc >= 3 ? argv[2] : std::string{};
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
  usage();
  return 2;
}
