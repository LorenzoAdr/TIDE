#include "ai/level2_session.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/ai_trace.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace {

const std::unordered_set<std::string>& l2_whitelist() {
  static const std::unordered_set<std::string> k = {
      "get_code_of",     "search",
      "repo_map",        "read_file",
      "list_files",      "workspace_symbols",
      "hover",           "diagnostics",
      "context_pack",    "list_tools",
      "file_outline",    "headers_of",
      "definition",      "references",
  };
  return k;
}

std::string json_escape(const std::string& s) {
  return ai_trace_escape(s, 8000);
}

std::string now_ms_str() {
  using clock = std::chrono::system_clock;
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
          .count();
  return std::to_string(ms);
}

std::string trim_ws(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' ||
                        s.front() == '\r')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' ||
                        s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return std::string(s);
}

std::string to_lower_copy(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s;
}

std::string path_stem_key(std::string_view path) {
  std::string p(path);
  // Drop trailing :line / :Symbol for get_code_of args.
  const auto colon = p.find(':');
  if (colon != std::string::npos) {
    // Keep Windows drive? We use unix paths in workspace.
    p = p.substr(0, colon);
  }
  const auto slash = p.find_last_of("/\\");
  std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
  const auto dot = base.find_last_of('.');
  if (dot != std::string::npos && dot > 0) {
    base = base.substr(0, dot);
  }
  return to_lower_copy(base);
}

void push_hot(std::vector<std::string>& hot, std::unordered_set<std::string>& seen,
              std::string key) {
  key = trim_ws(key);
  if (key.empty()) {
    return;
  }
  const std::string low = to_lower_copy(key);
  if (!seen.insert(low).second) {
    return;
  }
  hot.push_back(std::move(key));
}

bool is_ranked_entry_start(const std::string& line) {
  if (line.empty() || !std::isdigit(static_cast<unsigned char>(line[0]))) {
    return false;
  }
  std::size_t i = 0;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  return i < line.size() && line[i] == '.' && (i + 1 >= line.size() || line[i + 1] == ' ');
}

bool entry_matches_hot(const std::string& entry, const std::vector<std::string>& hot_keys) {
  if (hot_keys.empty()) {
    return false;
  }
  const std::string low = to_lower_copy(entry);
  for (const auto& raw : hot_keys) {
    const std::string h = to_lower_copy(raw);
    if (h.empty()) {
      continue;
    }
    if (low.find(h) != std::string::npos) {
      return true;
    }
    // stem=foo in why lines, or bare stem
    if (low.find("stem=" + h) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string first_line_of(const std::string& entry) {
  const auto nl = entry.find('\n');
  std::string line = nl == std::string::npos ? entry : entry.substr(0, nl);
  return trim_ws(line);
}

}  // namespace

std::vector<std::string> Level2Session::hot_keys_from_observations(const std::string& observations) {
  std::vector<std::string> hot;
  std::unordered_set<std::string> seen;

  // ### turn N — `tool` `arg`
  for (std::size_t pos = 0; (pos = observations.find("### turn ", pos)) != std::string::npos;) {
    const auto eol = observations.find('\n', pos);
    const std::string header =
        observations.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
    pos = eol == std::string::npos ? observations.size() : eol + 1;

    // Prefer last backtick-quoted token as arg.
    std::vector<std::string> ticks;
    for (std::size_t i = 0; i < header.size();) {
      const auto a = header.find('`', i);
      if (a == std::string::npos) {
        break;
      }
      const auto b = header.find('`', a + 1);
      if (b == std::string::npos) {
        break;
      }
      ticks.push_back(header.substr(a + 1, b - a - 1));
      i = b + 1;
    }
    if (ticks.size() >= 2) {
      const std::string& arg = ticks.back();
      push_hot(hot, seen, arg);
      // path without :Symbol
      const auto colon = arg.find(':');
      if (colon != std::string::npos && colon > 0) {
        push_hot(hot, seen, arg.substr(0, colon));
        const std::string sym = arg.substr(colon + 1);
        // Skip pure line numbers as hot keys (too broad vs scores).
        const bool all_digit =
            !sym.empty() &&
            std::all_of(sym.begin(), sym.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; });
        if (!all_digit) {
          push_hot(hot, seen, sym);
        }
      }
      push_hot(hot, seen, path_stem_key(arg));
    } else if (ticks.size() == 1 && ticks[0].find('/') != std::string::npos) {
      push_hot(hot, seen, ticks[0]);
      push_hot(hot, seen, path_stem_key(ticks[0]));
    }
  }

  // outline: path …
  for (std::size_t pos = 0; (pos = observations.find("outline:", pos)) != std::string::npos;) {
    const auto eol = observations.find('\n', pos);
    std::string line =
        observations.substr(pos + 8, eol == std::string::npos ? std::string::npos : eol - (pos + 8));
    pos = eol == std::string::npos ? observations.size() : eol + 1;
    const auto sym = line.find("symbols=");
    if (sym != std::string::npos) {
      line = line.substr(0, sym);
    }
    line = trim_ws(line);
    if (!line.empty()) {
      push_hot(hot, seen, line);
      push_hot(hot, seen, path_stem_key(line));
    }
  }

  return hot;
}

std::string Level2Session::compact_ranked_map_markdown(const std::string& map_section,
                                                       const std::vector<std::string>& hot_keys) {
  // map_section includes leading "## Ranked map" or starts after it.
  std::string body = map_section;
  std::string header;
  const std::string mark = "## Ranked map";
  if (body.rfind(mark, 0) == 0) {
    header = mark;
    body = body.substr(mark.size());
    if (!body.empty() && body[0] == '\n') {
      body.erase(body.begin());
    }
  }

  // Drop ## Bodies (and anything after) — detail for hot stems stays in Ranked entries / Observations.
  const auto bodies = body.find("\n## Bodies");
  if (bodies != std::string::npos) {
    body = body.substr(0, bodies);
  }
  const auto bodies_at0 = body.rfind("## Bodies", 0) == 0 ? std::size_t{0} : std::string::npos;
  if (bodies_at0 == 0) {
    body.clear();
  }

  // Split into preamble (query/note/## Ranked entries) + numbered entries.
  std::vector<std::string> entries;
  std::string preamble;
  {
    std::istringstream in(body);
    std::string line;
    std::ostringstream pre;
    std::ostringstream cur;
    bool in_entry = false;
    auto flush_entry = [&]() {
      if (!in_entry) {
        return;
      }
      std::string e = cur.str();
      while (!e.empty() && (e.back() == '\n' || e.back() == '\r')) {
        e.pop_back();
      }
      if (!e.empty()) {
        entries.push_back(std::move(e));
      }
      cur.str("");
      cur.clear();
    };
    while (std::getline(in, line)) {
      if (is_ranked_entry_start(line)) {
        if (!in_entry) {
          preamble = pre.str();
        }
        flush_entry();
        in_entry = true;
        cur << line << '\n';
      } else if (in_entry) {
        cur << line << '\n';
      } else {
        pre << line << '\n';
      }
    }
    flush_entry();
    if (!in_entry) {
      preamble = pre.str();
    }
  }

  int kept_detail = 0;
  std::ostringstream out;
  if (!header.empty()) {
    out << header << "\n\n";
  }
  out << preamble;
  if (preamble.find("## Ranked entries") == std::string::npos && !entries.empty()) {
    out << "## Ranked entries\n\n";
  }
  for (const auto& e : entries) {
    if (entry_matches_hot(e, hot_keys)) {
      out << e << "\n\n";
      ++kept_detail;
    } else {
      out << first_line_of(e) << "\n";
    }
  }
  out << "\n<!-- map compacted: full detail for " << kept_detail << " hot stem(s); "
      << "other entries are name-only -->\n";
  return out.str();
}

bool Level2Session::compact_session_context(const std::string& workspace_root,
                                            std::string* err_out) {
  if (workspace_root.empty()) {
    if (err_out) {
      *err_out = "workspace_root vacío";
    }
    return false;
  }
  std::string session = read_file(session_path(workspace_root));
  if (session.empty()) {
    if (err_out) {
      *err_out = "session.md ausente";
    }
    return false;
  }

  const std::string map_mark = "## Ranked map";
  const std::string obs_mark = "## Observations";
  const auto map_pos = session.find(map_mark);
  if (map_pos == std::string::npos) {
    return true;  // nothing to compact
  }
  const auto obs_pos = session.find(obs_mark, map_pos);
  const std::string before = session.substr(0, map_pos);
  std::string map_section;
  std::string after;
  if (obs_pos != std::string::npos) {
    map_section = session.substr(map_pos, obs_pos - map_pos);
    after = session.substr(obs_pos);
  } else {
    map_section = session.substr(map_pos);
  }

  // Skip if already compacted and no new hot keys would change cold entries... still cheap to redo.
  const auto hot = hot_keys_from_observations(after);
  // If explore has no observations yet, keep full map (caller shouldn't invoke, but be safe).
  if (hot.empty() && after.find("### turn ") == std::string::npos) {
    return true;
  }

  const std::string compact_map = compact_ranked_map_markdown(map_section, hot);
  session = before + compact_map;
  if (!after.empty()) {
    if (session.empty() || session.back() != '\n') {
      session.push_back('\n');
    }
    session += after;
  }
  session = trim_session_body(std::move(session));
  if (!write_file(session_path(workspace_root), session, err_out)) {
    return false;
  }
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"map_compact\",\"hot\":" +
                                   std::to_string(hot.size()) + "}");
  ai_trace(AiTraceChannel::L2, "l2_map_compact",
           "{\"hot\":" + std::to_string(hot.size()) +
               ",\"session_chars\":" + std::to_string(session.size()) + "}");
  return true;
}

Level2Session::Level2Session(Level2SessionDeps deps) : deps_(std::move(deps)) {}

Level2Session::Level2Session(ToolRegistry* tools) : deps_{tools, {}, {}} {}

std::string Level2Session::dir_for(const std::string& workspace_root) {
  return (fs::path(workspace_root) / ".tuide" / "ai" / "l2").lexically_normal().string();
}

std::string Level2Session::session_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "session.md").string();
}

std::string Level2Session::request_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "request.json").string();
}

std::string Level2Session::response_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "response.json").string();
}

std::string Level2Session::trace_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "trace.ndjson").string();
}

std::string Level2Session::state_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "state.json").string();
}

std::string Level2Session::pending_edits_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "pending_edits.json").string();
}

bool Level2Session::tool_allowed(const std::string& name) {
  return l2_whitelist().count(name) > 0;
}

std::string Level2Session::tool_guide_markdown() {
  return R"(## Tool guide

Fases: **explore** → (éxito) **edit** → **compile** → **done**.
Si explore **no** localiza el código: **clarify** (cancelar arreglo; pedir más detalle al usuario). No inventar edits.

### Explore / edit (lectura)
Preferir `get_code_of` / `file_outline` / `search` antes que `read_file`.

| tool | arg | ejemplo |
|------|-----|---------|
| get_code_of | path:Symbol \| path:line | `src/ai/ai_controller.cpp:wake` |
| file_outline | path | `src/ui/console_panel.cpp` |
| search | needles | `on_pty_output` |
| headers_of / definition / references | ver help | — |

### Acciones JSON (`request.json`)

Explore:
```json
{"action":"tool","name":"get_code_of","arg":"…"}
{"action":"done","summary":"código localizado en …","next":"edit"}
{"action":"done","summary":"no encontré X; ¿puedes concretar Y?","next":"clarify"}
```
- `next=edit` solo si hay evidencia en Observations (paths:líneas).
- `next=clarify` / `abort` cancela el arreglo y pide más explicaciones al usuario.
- `next` omitido = fin solo-lectura (sin edit).

Edit:
```json
{"action":"edit","hunks":[{"path":"src/foo.cpp","search":"…exact…","replace":"…"}]}
{"action":"tool","name":"get_code_of","arg":"…"}
```

Tras `edit` OK el runtime compila solo. Si falla (≤3): observation con stderr + old/new; reemite `edit`.
)";
}

std::string Level2Session::read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool Level2Session::write_file(const std::string& path, const std::string& body,
                               std::string* err) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  if (ec) {
    if (err) {
      *err = "no se pudo crear dir: " + ec.message();
    }
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir " + path;
    }
    return false;
  }
  out << body;
  return true;
}

Level2Session::State Level2Session::load_state(const std::string& workspace_root) {
  State st;
  const std::string raw = read_file(state_path(workspace_root));
  if (raw.empty()) {
    return st;
  }
  try {
    const auto j = nlohmann::json::parse(raw);
    st.turn = j.value("turn", 0);
    st.done = j.value("done", false);
    st.last_action = j.value("last_action", "");
    st.phase = j.value("phase", "explore");
    st.edit_attempt = j.value("edit_attempt", 0);
    st.compile_attempt = j.value("compile_attempt", 0);
    st.last_op_id = j.value("last_op_id", static_cast<uint64_t>(0));
    if (j.contains("pending") && j["pending"].is_array()) {
      for (const auto& p : j["pending"]) {
        PendingHunk h;
        h.path = p.value("path", "");
        h.abs_path = p.value("abs_path", "");
        h.old_text = p.value("old_text", "");
        h.new_text = p.value("new_text", "");
        h.before = p.value("before", "");
        st.pending.push_back(std::move(h));
      }
    }
  } catch (...) {
  }
  return st;
}

bool Level2Session::save_state(const std::string& workspace_root, const State& st,
                               std::string* err) {
  nlohmann::json pending = nlohmann::json::array();
  for (const auto& p : st.pending) {
    pending.push_back({{"path", p.path},
                       {"abs_path", p.abs_path},
                       {"old_text", p.old_text},
                       {"new_text", p.new_text},
                       {"before", p.before}});
  }
  nlohmann::json j = {{"turn", st.turn},
                      {"done", st.done},
                      {"last_action", st.last_action},
                      {"phase", st.phase},
                      {"edit_attempt", st.edit_attempt},
                      {"compile_attempt", st.compile_attempt},
                      {"last_op_id", st.last_op_id},
                      {"pending", pending}};
  return write_file(state_path(workspace_root), j.dump(2) + "\n", err);
}

std::string Level2Session::truncate_observation(const std::string& text, int max_lines) {
  if (max_lines <= 0) {
    return text;
  }
  std::istringstream in(text);
  std::ostringstream out;
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    out << line << '\n';
    if (++n >= max_lines) {
      out << "… (observation truncated at " << max_lines << " lines)\n";
      break;
    }
  }
  return out.str();
}

std::string Level2Session::truncate_observation_tail(const std::string& text, int max_lines) {
  if (max_lines <= 0 || text.empty()) {
    return text;
  }
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(max_lines) + 8);
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }
  if (static_cast<int>(lines.size()) <= max_lines) {
    std::ostringstream out;
    for (const auto& l : lines) {
      out << l << '\n';
    }
    return out.str();
  }
  std::ostringstream out;
  out << "… (showing last " << max_lines << " of " << lines.size() << " lines)\n";
  const std::size_t start = lines.size() - static_cast<std::size_t>(max_lines);
  for (std::size_t i = start; i < lines.size(); ++i) {
    out << lines[i] << '\n';
  }
  return out.str();
}

std::string Level2Session::trim_session_body(std::string body) {
  if (body.size() <= kCharBudget) {
    return body;
  }
  const std::string obs_mark = "## Observations";
  const auto obs_pos = body.find(obs_mark);
  if (obs_pos == std::string::npos) {
    return body.substr(0, kCharBudget) + "\n\n<!-- truncated hard -->\n";
  }
  const std::string head = body.substr(0, obs_pos);
  std::string obs_section = body.substr(obs_pos);
  std::vector<std::size_t> turns;
  std::size_t pos = 0;
  while (true) {
    const auto p = obs_section.find("\n### turn ", pos);
    if (p == std::string::npos) {
      break;
    }
    turns.push_back(p + 1);
    pos = p + 1;
  }
  const auto p0 = obs_section.find("### turn ");
  if (p0 != std::string::npos && (turns.empty() || turns.front() != p0)) {
    turns.insert(turns.begin(), p0);
  }
  std::size_t drop = 0;
  while (drop < turns.size() && head.size() + obs_section.size() > kCharBudget) {
    ++drop;
    std::ostringstream rebuilt;
    rebuilt << "## Observations\n\n<!-- older observations trimmed (" << drop << ") -->\n\n";
    if (drop < turns.size()) {
      rebuilt << obs_section.substr(turns[drop]);
    }
    obs_section = rebuilt.str();
  }
  body = head + obs_section;
  if (body.size() > kCharBudget) {
    body.resize(kCharBudget);
    body += "\n<!-- session hard-truncated -->\n";
  }
  return body;
}

void Level2Session::append_trace(const std::string& workspace_root,
                                 const std::string& json_line) {
  std::error_code ec;
  fs::create_directories(dir_for(workspace_root), ec);
  std::ofstream out(trace_path(workspace_root), std::ios::app);
  if (!out) {
    return;
  }
  out << json_line;
  if (json_line.empty() || json_line.back() != '\n') {
    out << '\n';
  }
}

void Level2Session::write_response_json(const std::string& workspace_root, bool ok,
                                        const std::string& action, const std::string& name,
                                        const std::string& arg, const std::string& text,
                                        const std::string& error, int turn,
                                        const std::string& phase) {
  nlohmann::json j = {{"ok", ok},
                      {"action", action},
                      {"name", name},
                      {"arg", arg},
                      {"turn", turn},
                      {"error", error},
                      {"text", text},
                      {"phase", phase}};
  std::string err;
  write_file(response_path(workspace_root), j.dump(2) + "\n", &err);
}

bool Level2Session::append_observation(const std::string& workspace_root, const std::string& block,
                                       std::size_t* session_chars, std::string* err) {
  std::string session = read_file(session_path(workspace_root));
  if (session.empty()) {
    if (err) {
      *err = "session.md ausente";
    }
    return false;
  }
  const std::string empty_marker = "## Observations\n\n(vacío — L2 pide tools)\n";
  if (session.find(empty_marker) != std::string::npos) {
    const auto pos = session.find(empty_marker);
    session.replace(pos, empty_marker.size(), std::string("## Observations\n\n") + block);
  } else if (session.find("## Observations") == std::string::npos) {
    session += "\n## Observations\n\n";
    session += block;
  } else {
    session += block;
  }
  session = trim_session_body(std::move(session));
  if (session_chars) {
    *session_chars = session.size();
  }
  return write_file(session_path(workspace_root), session, err);
}

bool Level2Session::bootstrap(const Level2BootstrapOpts& opts, std::string* err_out) {
  if (opts.workspace_root.empty()) {
    if (err_out) {
      *err_out = "workspace_root vacío";
    }
    return false;
  }
  std::error_code ec;
  fs::create_directories(dir_for(opts.workspace_root), ec);
  if (ec) {
    if (err_out) {
      *err_out = ec.message();
    }
    return false;
  }

  std::string map_path = opts.map_path;
  if (map_path.empty()) {
    map_path =
        (fs::path(opts.workspace_root) / ".tuide" / "ai" / "map_last.md").lexically_normal().string();
  }
  std::string map_body = read_file(map_path);
  if (map_body.empty()) {
    map_body = "(sin map_last.md — L1 aún no escribió el mapa)\n";
  } else if (map_body.rfind("# ", 0) == 0) {
    const auto nl = map_body.find('\n');
    if (nl != std::string::npos) {
      map_body = map_body.substr(nl + 1);
      while (!map_body.empty() && (map_body[0] == '\n' || map_body[0] == '\r')) {
        map_body.erase(map_body.begin());
      }
    }
  }

  std::ostringstream md;
  md << "# L2 session (harness)\n\n";
  md << tool_guide_markdown() << "\n";
  md << "## Instruction\n\n";
  md << "query: " << opts.query << "\n\n";
  if (!opts.instruction.empty()) {
    md << "instruction: " << opts.instruction << "\n\n";
  }
  if (!opts.seeds.empty()) {
    md << "seeds:";
    for (const auto& s : opts.seeds) {
      md << ' ' << s;
    }
    md << "\n\n";
  }
  md << "Fase inicial: **explore**. El ## Ranked map es el punto de partida: elige qué leer "
        "con tools y decide el cambio. Al terminar: "
        "`{\"action\":\"done\",\"summary\":\"…\",\"next\":\"edit\"}`.\n\n";
  md << "## Ranked map\n\n";
  md << map_body;
  if (!map_body.empty() && map_body.back() != '\n') {
    md << '\n';
  }
  md << "\n## Observations\n\n";
  md << "(vacío — L2 pide tools)\n";

  std::string body = trim_session_body(md.str());
  if (!write_file(session_path(opts.workspace_root), body, err_out)) {
    return false;
  }

  State st;
  st.phase = "explore";
  st.last_action = "bootstrap";
  if (!save_state(opts.workspace_root, st, err_out)) {
    return false;
  }

  write_file(request_path(opts.workspace_root),
             "{\n  \"action\": \"tool\",\n  \"name\": \"get_code_of\",\n  \"arg\": \"\"\n}\n",
             nullptr);
  write_response_json(opts.workspace_root, true, "bootstrap", "", "", "session ready", "", 0,
                      "explore");
  append_trace(opts.workspace_root,
               std::string("{\"ts\":") + now_ms_str() +
                   ",\"event\":\"bootstrap\",\"query\":\"" + json_escape(opts.query) +
                   "\",\"phase\":\"explore\"}");
  ai_trace(AiTraceChannel::L2, "l2_bootstrap",
           std::string("{\"path\":\"") + json_escape(session_path(opts.workspace_root)) + "\"}");
  return true;
}

Level2TurnResult Level2Session::apply_tool(const std::string& workspace_root,
                                           const std::string& name, const std::string& arg) {
  Level2TurnResult out;
  out.action = "tool";
  out.name = name;
  out.arg = arg;
  State st = load_state(workspace_root);
  out.phase = st.phase;

  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (st.done || st.phase == "done") {
    out.error = "sesión done; reinicia con bootstrap";
    return out;
  }
  if (st.phase != "explore" && st.phase != "edit") {
    out.error = "tools solo en phase explore|edit (ahora=" + st.phase + ")";
    write_response_json(workspace_root, false, "error", name, arg, "", out.error, st.turn, st.phase);
    return out;
  }
  if (!tool_allowed(name)) {
    out.error = "tool no permitido: " + name;
    write_response_json(workspace_root, false, "error", name, arg, "", out.error, st.turn, st.phase);
    return out;
  }
  if (deps_.tools == nullptr || !deps_.tools->has(name)) {
    out.error = "tool no registrado: " + name;
    write_response_json(workspace_root, false, "error", name, arg, "", out.error, st.turn, st.phase);
    return out;
  }

  const auto t0 = std::chrono::steady_clock::now();
  const AiToolResult tr = deps_.tools->invoke(name, arg);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  ++st.turn;
  st.last_action = "tool:" + name;
  out.turn = st.turn;

  const std::string obs_text = truncate_observation(tr.text, kMaxObservationLines);
  std::ostringstream block;
  block << "### turn " << st.turn << " — `" << name << "`";
  if (!arg.empty()) {
    block << " `" << arg << "`";
  }
  block << "\n\n```\n" << obs_text;
  if (!obs_text.empty() && obs_text.back() != '\n') {
    block << '\n';
  }
  block << "```\n\n";

  std::string err;
  if (!append_observation(workspace_root, block.str(), &out.session_chars, &err)) {
    out.error = err;
    return out;
  }
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, tr.ok, "tool", name, arg, obs_text,
                      tr.ok ? "" : "tool failed", st.turn, st.phase);
  std::ostringstream tj;
  tj << "{\"ts\":" << now_ms_str() << ",\"event\":\"tool\",\"turn\":" << st.turn
     << ",\"name\":\"" << json_escape(name) << "\",\"ok\":" << (tr.ok ? "true" : "false")
     << ",\"ms\":" << ms << ",\"phase\":\"" << st.phase << "\"}";
  append_trace(workspace_root, tj.str());
  ai_trace(AiTraceChannel::L2, "l2_tool",
           "{\"turn\":" + std::to_string(st.turn) + ",\"name\":\"" + ai_trace_escape(name) +
               "\",\"ok\":" + (tr.ok ? "1" : "0") + ",\"duration_ms\":" + std::to_string(ms) +
               ",\"phase\":\"" + st.phase + "\",\"arg\":\"" + ai_trace_escape(arg, 120) + "\"}");
  // Shrink ranked map: name-only except stems/paths this explore already touched.
  if (st.phase == "explore") {
    compact_session_context(workspace_root, nullptr);
    out.session_chars = read_file(session_path(workspace_root)).size();
  }
  out.ok = true;
  out.summary = "tool turn=" + std::to_string(st.turn);
  return out;
}

Level2TurnResult Level2Session::apply_edit(const std::string& workspace_root,
                                           const std::vector<SearchReplaceHunk>& hunks) {
  Level2TurnResult out;
  out.action = "edit";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  const auto edit_t0 = std::chrono::steady_clock::now();
  auto edit_ms = [&]() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 edit_t0)
        .count();
  };
  if (st.phase != "edit") {
    out.error = "action=edit solo en phase=edit (ahora=" + st.phase + ")";
    write_response_json(workspace_root, false, "error", "edit", "", "", out.error, st.turn, st.phase);
    return out;
  }
  auto record_edit_failure = [&](const std::string& err_msg, const std::string& path,
                                 const std::string& search, const std::string& replace) {
    const auto ms = edit_ms();
    ++st.turn;
    st.last_action = "edit_feedback";
    out.turn = st.turn;
    out.error = err_msg;
    out.summary = err_msg;
    out.ok = false;
    out.phase = "edit";

    std::ostringstream block;
    block << "### turn " << st.turn << " — edit_feedback\n\n";
    block << "error: " << err_msg << "\n\n";
    if (!path.empty()) {
      block << "path: `" << path << "`\n\n";
    }
    if (!search.empty()) {
      block << "## search (failed)\n\n```\n"
            << truncate_observation(search, 24) << "```\n\n";
    }
    if (!replace.empty()) {
      block << "## replace (intended)\n\n```\n"
            << truncate_observation(replace, 24) << "```\n\n";
    }
    block << "Reemite `action=edit` con `search` exacto y único (get_code_of si hace falta). "
             "No repitas el mismo hunk fallido.\n\n";
    std::string obs_err;
    append_observation(workspace_root, block.str(), &out.session_chars, &obs_err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "edit", "", path, block.str(), err_msg, st.turn,
                        "edit");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"edit_fail\",\"turn\":" +
                                     std::to_string(st.turn) + ",\"ms\":" + std::to_string(ms) +
                                     ",\"error\":\"" + json_escape(err_msg) + "\"}");
    ai_trace(AiTraceChannel::L2, "l2_edit",
             "{\"turn\":" + std::to_string(st.turn) + ",\"ok\":0,\"hunks\":" +
                 std::to_string(hunks.size()) + ",\"duration_ms\":" + std::to_string(ms) +
                 ",\"error\":\"" + ai_trace_escape(err_msg) + "\"}");
  };

  if (hunks.empty()) {
    record_edit_failure("hunks vacío", "", "", "");
    return out;
  }

  std::vector<ApplyHunkResult> applied;
  applied.reserve(hunks.size());
  for (const auto& h : hunks) {
    ApplyHunkResult r = apply_hunk_to_workspace_file(workspace_root, h, /*write=*/true);
    if (!r.ok) {
      // Rollback any already written hunks in this batch.
      for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
        write_text_file(it->abs_path, it->before, nullptr);
      }
      record_edit_failure("hunk falló (" + h.path + "): " + r.error, h.path, h.search, h.replace);
      return out;
    }
    if (deps_.sync_edit) {
      deps_.sync_edit(r);
    }
    applied.push_back(std::move(r));
  }

  ++st.turn;
  ++st.edit_attempt;
  st.last_action = "edit";
  st.last_op_id = static_cast<uint64_t>(st.turn);
  st.pending.clear();
  for (std::size_t i = 0; i < applied.size(); ++i) {
    PendingHunk p;
    p.path = hunks[i].path;
    p.abs_path = applied[i].abs_path;
    p.old_text = applied[i].old_text;
    p.new_text = applied[i].new_text;
    p.before = applied[i].before;
    st.pending.push_back(std::move(p));
  }
  out.turn = st.turn;

  std::ostringstream block;
  block << "### turn " << st.turn << " — edit\n\n";
  block << "hunks=" << applied.size() << " edit_attempt=" << st.edit_attempt << "\n\n";
  for (std::size_t i = 0; i < applied.size(); ++i) {
    block << "#### hunk " << (i + 1) << " `" << hunks[i].path << "` @"
          << applied[i].span.start_line << "\n\n";
    block << "```diff\n- " << truncate_observation(applied[i].old_text, 40);
    block << "+ " << truncate_observation(applied[i].new_text, 40) << "```\n\n";
  }
  std::string err;
  if (!append_observation(workspace_root, block.str(), &out.session_chars, &err)) {
    out.error = err;
    return out;
  }
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, true, "edit", "", "", block.str(), "", st.turn, "edit");
  const auto apply_ms = edit_ms();
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"edit\",\"turn\":" + std::to_string(st.turn) +
                                   ",\"hunks\":" + std::to_string(applied.size()) +
                                   ",\"ms\":" + std::to_string(apply_ms) + "}");
  ai_trace(AiTraceChannel::L2, "l2_edit",
           "{\"turn\":" + std::to_string(st.turn) + ",\"ok\":1,\"hunks\":" +
               std::to_string(applied.size()) + ",\"duration_ms\":" + std::to_string(apply_ms) +
               "}");
  out.ok = true;
  out.summary = "edit applied; compiling…";
  return after_successful_edit(workspace_root, st);
}

Level2TurnResult Level2Session::after_successful_edit(const std::string& workspace_root,
                                                      State st) {
  st.phase = "compile";
  save_state(workspace_root, st, nullptr);
  return run_compile(workspace_root);
}

Level2TurnResult Level2Session::run_compile(const std::string& workspace_root) {
  Level2TurnResult out;
  out.action = "compile";
  State st = load_state(workspace_root);
  out.phase = st.phase;

  ++st.compile_attempt;
  ++st.turn;
  st.last_action = "compile";
  out.turn = st.turn;

  const auto compile_t0 = std::chrono::steady_clock::now();
  int exit_code = -1;
  std::string output;
  if (deps_.run_compile) {
    exit_code = deps_.run_compile(&output);
  } else {
    const std::string log = (fs::path(workspace_root) / ".tuide" / "ai" / "l2" / "compile.log")
                                .lexically_normal()
                                .string();
    const std::string cmd = "cd " + workspace_root + " && ./tools/compile.sh -y >" + log +
                            " 2>&1; echo $?";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
      char buf[64] = {};
      if (fgets(buf, sizeof(buf), pipe)) {
        exit_code = std::atoi(buf);
      }
      pclose(pipe);
    }
    output = read_file(log);
  }
  const auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - compile_t0)
                              .count();
  ai_trace(AiTraceChannel::L2, "l2_compile",
           "{\"turn\":" + std::to_string(st.turn) + ",\"attempt\":" +
               std::to_string(st.compile_attempt) + ",\"exit\":" + std::to_string(exit_code) +
               ",\"ok\":" + (exit_code == 0 ? "1" : "0") +
               ",\"duration_ms\":" + std::to_string(compile_ms) + "}");

  const std::string trunc = truncate_observation_tail(output, kMaxCompileLogLines);
  std::ostringstream block;
  if (exit_code == 0) {
    block << "### turn " << st.turn << " — compile\n\n";
    block << "attempt: " << st.compile_attempt << "/" << kMaxCompileAttempts
          << "  exit_code: " << exit_code << "\n\n";
    // Success: keep log tiny (compiler noise rarely needed).
    const std::string ok_log = truncate_observation_tail(output, 12);
    block << "```\n" << ok_log;
    if (!ok_log.empty() && ok_log.back() != '\n') {
      block << '\n';
    }
    block << "```\n\n";
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);

    st.phase = "done";
    st.done = true;
    st.last_action = "compile_ok";
    st.pending.clear();
    save_state(workspace_root, st, nullptr);
    std::ostringstream summary;
    summary << "OK compile. edit_attempts=" << st.edit_attempt
            << " compile_attempts=" << st.compile_attempt;
    std::ostringstream done_block;
    done_block << "### turn " << (++st.turn) << " — done\n\n" << summary.str() << "\n\n";
    append_observation(workspace_root, done_block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, true, "done", "compile", "", summary.str(), "", st.turn,
                        "done");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"compile_ok\",\"exit\":0,\"ms\":" +
                                     std::to_string(compile_ms) + "}");
    out.ok = true;
    out.action = "done";
    out.summary = summary.str();
    out.phase = "done";
    out.turn = st.turn;
    return out;
  }

  // Fail: single observation (no duplicate stderr) — tail only + old/new hunks.
  block << "### turn " << st.turn << " — compile_feedback\n\n";
  block << "attempt: " << st.compile_attempt << "/" << kMaxCompileAttempts << "\n";
  block << "exit_code: " << exit_code << "\n\n";
  block << "```stderr (tail)\n" << trunc;
  if (!trunc.empty() && trunc.back() != '\n') {
    block << '\n';
  }
  block << "```\n\n";
  for (std::size_t i = 0; i < st.pending.size(); ++i) {
    const auto& p = st.pending[i];
    block << "## old (hunk " << (i + 1) << " `" << p.path << "`)\n\n```\n"
          << truncate_observation(p.old_text, 24) << "```\n\n";
    block << "## new (hunk " << (i + 1) << " `" << p.path << "`)\n\n```\n"
          << truncate_observation(p.new_text, 24) << "```\n\n";
  }
  block << "Reemite `action=edit` corrigiendo el error (o el runtime hará rollback si se agotan "
           "intentos).\n\n";
  std::string err;
  append_observation(workspace_root, block.str(), &out.session_chars, &err);

  if (st.compile_attempt >= kMaxCompileAttempts) {
    // Rollback files to before.
    for (const auto& p : st.pending) {
      if (!p.before.empty() && !p.abs_path.empty()) {
        write_text_file(p.abs_path, p.before, nullptr);
      }
    }
    st.phase = "done";
    st.done = true;
    st.last_action = "compile_fail_rollback";
    save_state(workspace_root, st, nullptr);
    std::ostringstream summary;
    summary << "FAIL compile x" << st.compile_attempt << "; rollback aplicado.";
    std::ostringstream done_block;
    done_block << "### turn " << (++st.turn) << " — done\n\n" << summary.str() << "\n\n";
    append_observation(workspace_root, done_block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "done", "compile", "", summary.str(), summary.str(),
                        st.turn, "done");
    out.ok = false;
    out.action = "done";
    out.summary = summary.str();
    out.phase = "done";
    out.turn = st.turn;
    out.error = summary.str();
    return out;
  }

  st.phase = "edit";
  st.last_action = "compile_feedback";
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, false, "compile", "", "", trunc,
                      "compile failed; re-edit", st.turn, "edit");
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"compile_fail\",\"exit\":" +
                                   std::to_string(exit_code) + ",\"attempt\":" +
                                   std::to_string(st.compile_attempt) + ",\"ms\":" +
                                   std::to_string(compile_ms) + "}");
  out.ok = false;
  out.summary = "compile failed; phase=edit again";
  out.phase = "edit";
  out.error = "compile exit_code=" + std::to_string(exit_code);
  return out;
}

Level2TurnResult Level2Session::rollback_pending(const std::string& workspace_root) {
  Level2TurnResult out;
  out.action = "rollback";
  State st = load_state(workspace_root);
  for (const auto& p : st.pending) {
    if (!p.before.empty() && !p.abs_path.empty()) {
      write_text_file(p.abs_path, p.before, nullptr);
    }
  }
  st.pending.clear();
  st.last_action = "rollback";
  save_state(workspace_root, st, nullptr);
  out.ok = true;
  out.summary = "rollback ok";
  out.phase = st.phase;
  return out;
}

Level2TurnResult Level2Session::mark_done(const std::string& workspace_root,
                                          const std::string& summary, const std::string& next) {
  Level2TurnResult out;
  out.action = "done";
  out.summary = summary;
  State st = load_state(workspace_root);
  out.phase = st.phase;

  if (next == "edit") {
    if (st.phase != "explore") {
      out.error = "next=edit solo desde explore";
      return out;
    }
    // Drop full map bodies before edit phase prompts (keep detail only for hot stems).
    compact_session_context(workspace_root, nullptr);
    ++st.turn;
    st.phase = "edit";
    st.last_action = "ready_to_edit";
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — ready_to_edit\n\n";
    block << summary << "\n\n";
    block << "Fase **edit**: emite `action=edit` con hunks Search/Replace.\n\n";
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, true, "done", "", "", summary, "", st.turn, "edit");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"phase_edit\",\"turn\":" +
                                     std::to_string(st.turn) + "}");
    out.ok = true;
    out.phase = "edit";
    out.summary = "phase=edit";
    return out;
  }

  if (next == "clarify" || next == "abort" || next == "need_info") {
    if (st.phase != "explore" && st.phase != "edit") {
      out.error = "next=clarify solo desde explore|edit";
      return out;
    }
    ++st.turn;
    st.done = true;
    st.phase = "clarify";
    st.last_action = "need_clarification";
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — clarify (arreglo cancelado)\n\n";
    block << summary << "\n\n";
    block << "_No se pasa a edit/compile. El usuario debe dar más detalle y relanzar._\n\n";
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, true, "done", "clarify", "", summary, "", st.turn,
                        "clarify");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"need_clarification\",\"turn\":" +
                                     std::to_string(st.turn) + "}");
    out.ok = true;
    out.phase = "clarify";
    out.summary = summary.empty() ? "need clarification" : summary;
    return out;
  }

  ++st.turn;
  st.done = true;
  st.phase = "done";
  st.last_action = "done";
  out.turn = st.turn;
  std::ostringstream block;
  block << "### turn " << st.turn << " — done\n\n" << summary << "\n\n";
  std::string err;
  append_observation(workspace_root, block.str(), &out.session_chars, &err);
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, true, "done", "", "", summary, "", st.turn, "done");
  out.ok = true;
  out.phase = "done";
  return out;
}

Level2TurnResult Level2Session::process_request_file(const std::string& workspace_root) {
  Level2TurnResult out;
  const std::string raw = read_file(request_path(workspace_root));
  if (raw.empty()) {
    out.error = "request.json vacío o ausente";
    write_response_json(workspace_root, false, "error", "", "", "", out.error, 0);
    return out;
  }
  try {
    const auto j = nlohmann::json::parse(raw);
    const std::string action = j.value("action", "");
    const State st = load_state(workspace_root);
    out.phase = st.phase;

    if (action == "tool") {
      return apply_tool(workspace_root, j.value("name", ""), j.value("arg", ""));
    }
    if (action == "done") {
      return mark_done(workspace_root, j.value("summary", ""), j.value("next", ""));
    }
    if (action == "edit") {
      std::string err;
      auto hunks = parse_search_replace_json(j, &err);
      if (hunks.empty()) {
        out.error = err.empty() ? "edit sin hunks" : err;
        write_response_json(workspace_root, false, "error", "edit", "", "", out.error, st.turn,
                            st.phase);
        return out;
      }
      return apply_edit(workspace_root, hunks);
    }
    if (action == "compile") {
      // Runtime-owned; allow manual trigger only if phase=compile or edit (force).
      return run_compile(workspace_root);
    }
    out.error = "action desconocida: " + action;
    write_response_json(workspace_root, false, "error", "", "", "", out.error, 0, st.phase);
    return out;
  } catch (const std::exception& ex) {
    out.error = std::string("JSON inválido: ") + ex.what();
    write_response_json(workspace_root, false, "error", "", "", "", out.error, 0);
    return out;
  }
}

std::string Level2Session::status_text(const std::string& workspace_root) const {
  std::ostringstream out;
  out << "=== L2 harness session ===\n";
  out << "dir: " << dir_for(workspace_root) << '\n';
  const State st = load_state(workspace_root);
  out << "phase: " << st.phase << "  turn: " << st.turn << "  done: " << (st.done ? "yes" : "no")
      << "\n";
  out << "edit_attempt=" << st.edit_attempt << " compile_attempt=" << st.compile_attempt
      << " pending_hunks=" << st.pending.size() << "\n";
  out << "last: " << (st.last_action.empty() ? "-" : st.last_action) << '\n';
  const std::string session = read_file(session_path(workspace_root));
  out << "session.md: "
      << (session.empty() ? "missing" : (std::to_string(session.size()) + " chars")) << '\n';
  return out.str();
}

}  // namespace tuide
